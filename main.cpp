#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <memory>

#include "config/config.hpp"

struct StreamState {
    std::vector<char> buf;
    size_t offset = 0; // how much of buf has been sent already
    bool want_write = false; // is EPOLLOUT currently registered
    bool want_read = true; // is EPOLLIN currently registered on the peer
    bool closed = false; // did the reading side see EOF
    bool write_shutdown = false; // did we shutdown the writing side
};

struct Session {
    int client_fd = -1;
    int upstream_fd = -1;
    sockaddr_in client_addr{};
    StreamState client_to_upstream; // buffered client -> upstream data
    StreamState upstream_to_client; // buffered upstream -> client data

    ~Session() {
        if (client_fd != -1) close(client_fd);
        if (upstream_fd != -1) close(upstream_fd);
    }
};

// Opens a non-blocking TCP connection to the upstream server.
// Returns the connecting fd, or -1 on failure.
static int connect_to_upstream(const std::string &host, int port) {
    sockaddr_in up{};
    up.sin_family = AF_INET;
    up.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &up.sin_addr) != 1) {
        std::cerr << "upstream: invalid address: " << host << '\n';
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd == -1) {
        perror("upstream socket");
        return -1;
    }

    if (connect(fd, reinterpret_cast<sockaddr *>(&up), sizeof(up)) == -1) {
        if (errno != EINPROGRESS) {
            perror("upstream connect");
            close(fd);
            return -1;
        }
    }

    return fd;
}

static void update_epoll(int epoll_fd, int fd, bool want_read, bool want_write) {
    epoll_event ev{};
    if (want_read) ev.events |= EPOLLIN;
    if (want_write) ev.events |= EPOLLOUT;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        // perror("epoll_ctl mod"); // Can happen if FD is already closed/invalid
    }
}

// Reads from `src_fd` into `out_state.buf`.
static bool handle_read(int src_fd, StreamState &out_state, size_t max_buf_size, std::vector<char> &scratch) {
    if (out_state.closed) return true;

    while (true) {
        size_t pending = out_state.buf.size() - out_state.offset;
        if (pending >= max_buf_size) {
            if (out_state.want_read) {
                out_state.want_read = false;
                break;
            }
        }

        ssize_t r = recv(src_fd, scratch.data(), scratch.size(), 0);
        if (r == 0) {
            out_state.closed = true;
            out_state.want_read = false;
            break; // EOF
        }
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // drained
            if (errno == EINTR) continue;
            // Unrecoverable read error
            return false;
        }

        out_state.buf.insert(out_state.buf.end(), scratch.begin(), scratch.begin() + r);
    }
    return true;
}

// Attempts to send data from `state.buf` out to `dst_fd`.
// Sets `state.want_write` accordingly.
static bool try_write(int dst_fd, StreamState &state) {
    while (state.offset < state.buf.size()) {
        ssize_t w = send(dst_fd, state.buf.data() + state.offset, state.buf.size() - state.offset, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                state.want_write = true;
                return true;
            }
            if (errno == EINTR) continue;
            return false; // connection broken
        }
        state.offset += w;
    }

    // Fully drained
    state.buf.clear();
    state.offset = 0;
    state.want_write = false;
    return true;
}

static void check_write_shutdown(int dst_fd, StreamState &state) {
    if (state.closed && state.buf.size() == state.offset && !state.write_shutdown) {
        shutdown(dst_fd, SHUT_WR);
        state.write_shutdown = true;
        state.want_write = false;
    }
}

int main() {
    Config cfg;
    try {
        cfg = load_config("./../config.toml");
    } catch (const std::exception &e) {
        std::cerr << "Config error: " << e.what() << '\n';
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(cfg.port);

    if (bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    const int backlog = cfg.backlog > 0 ? cfg.backlog : SOMAXCONN;
    if (listen(server_fd, backlog) == -1) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(server_fd);
        return 1;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
        close(epoll_fd);
        close(server_fd);
        return 1;
    }

    std::cout << "Listening on port " << cfg.port
            << " → upstream " << cfg.upstream_host << ':' << cfg.upstream_port << '\n';

    std::unordered_map<int, std::shared_ptr<Session> > sessions;

    std::vector<epoll_event> events(cfg.max_events);
    std::vector<char> scratch(65536);

    auto remove_session = [&](std::shared_ptr<Session> sess) {
        if (!sess) return;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sess->client_addr.sin_addr, ip, sizeof(ip));
        std::cout << "Session " << ip << ':' << ntohs(sess->client_addr.sin_port) << " disconnected\n";

        if (sess->client_fd != -1) {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sess->client_fd, nullptr);
            sessions.erase(sess->client_fd);
            close(sess->client_fd); // must close here — by the time the destructor
            // runs, client_fd has already been reset below
            sess->client_fd = -1; // guards against double-close if called again
        }
        if (sess->upstream_fd != -1) {
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sess->upstream_fd, nullptr);
            sessions.erase(sess->upstream_fd);
            close(sess->upstream_fd);
            sess->upstream_fd = -1;
        }
    };

    while (true) {
        int n = epoll_wait(epoll_fd, events.data(), cfg.max_events, -1);
        if (n == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev_mask = events[i].events;

            if (fd == server_fd) {
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);

                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("accept");
                        break;
                    }

                    fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL) | O_NONBLOCK);

                    int up_fd = connect_to_upstream(cfg.upstream_host, cfg.upstream_port);
                    if (up_fd == -1) {
                        close(client_fd);
                        continue;
                    }

                    auto sess = std::make_shared<Session>();
                    sess->client_fd = client_fd;
                    sess->upstream_fd = up_fd;
                    sess->client_addr = client_addr;

                    // We expect to write to both immediately. Upstream because it just connected.
                    sess->client_to_upstream.want_write = true; // wait for connect to finish

                    sessions[client_fd] = sess;
                    sessions[up_fd] = sess;

                    epoll_event cev{};
                    cev.events = EPOLLIN;
                    cev.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &cev);

                    epoll_event uev{};
                    uev.events = EPOLLIN | EPOLLOUT;
                    uev.data.fd = up_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, up_fd, &uev);

                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
                    std::cout << "Session established for client " << ip << ':' << ntohs(client_addr.sin_port) << '\n';
                }
                continue;
            }

            auto it = sessions.find(fd);
            if (it == sessions.end()) continue;
            auto sess = it->second;

            bool is_client = (fd == sess->client_fd);

            StreamState &in_state = is_client ? sess->client_to_upstream : sess->upstream_to_client;
            StreamState &out_state = is_client ? sess->upstream_to_client : sess->client_to_upstream;
            int peer_fd = is_client ? sess->upstream_fd : sess->client_fd;

            bool alive = true;

            // Handle Read (from fd, buffers into in_state)
            if (ev_mask & EPOLLIN) {
                if (!handle_read(fd, in_state, cfg.buffer_size, scratch)) {
                    alive = false;
                }
            }

            // Immediately try writing any new data to peer
            if (alive && in_state.buf.size() > in_state.offset) {
                if (!try_write(peer_fd, in_state)) {
                    alive = false;
                }
                // Check backpressure relief
                if (!in_state.want_read && (in_state.buf.size() - in_state.offset) < cfg.buffer_size) {
                    in_state.want_read = true;
                    // Note: update_epoll for fd will be called at the end of the loop
                }
            }

            if (alive) {
                check_write_shutdown(peer_fd, in_state);
            }

            // Handle Write (to fd, drains from out_state)
            if (alive && (ev_mask & EPOLLOUT)) {
                if (!try_write(fd, out_state)) {
                    alive = false;
                }

                // If we drained the buffer, relieve backpressure on the peer so they can read again
                if (alive && (out_state.buf.size() - out_state.offset) < cfg.buffer_size && !out_state.want_read) {
                    out_state.want_read = true;
                    update_epoll(epoll_fd, peer_fd, out_state.want_read, in_state.want_write);
                }
            }

            if (alive) {
                check_write_shutdown(fd, out_state);
            }

            if (ev_mask & EPOLLERR) {
                alive = false;
            }

            if (sess->client_to_upstream.write_shutdown && sess->upstream_to_client.write_shutdown) {
                alive = false;
            }

            if (!alive) {
                remove_session(sess);
            } else {
                // Update epoll interests if they changed
                update_epoll(epoll_fd, fd, in_state.want_read, out_state.want_write);
                update_epoll(epoll_fd, peer_fd, out_state.want_read, in_state.want_write);
            }
        }
    }

    close(epoll_fd);
    close(server_fd);
}
