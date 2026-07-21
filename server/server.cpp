#include "server.hpp"
#include "../net/utils.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

Server::Server(const Config &cfg) : m_cfg(cfg) {
    m_server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (m_server_fd == -1) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(m_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(m_cfg.port);

    if (bind(m_server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1) {
        perror("bind");
        close(m_server_fd);
        exit(1);
    }

    const int backlog = m_cfg.backlog > 0 ? m_cfg.backlog : SOMAXCONN;
    if (listen(m_server_fd, backlog) == -1) {
        perror("listen");
        close(m_server_fd);
        exit(1);
    }

    m_epoll_fd = epoll_create1(0);
    if (m_epoll_fd == -1) {
        perror("epoll_create1");
        close(m_server_fd);
        exit(1);
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = m_server_fd;
    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_server_fd, &ev) == -1) {
        perror("epoll_ctl: server_fd");
        close(m_epoll_fd);
        close(m_server_fd);
        exit(1);
    }

    std::cout << "Listening on port " << m_cfg.port
            << " -> upstream " << m_cfg.upstream_host << ':' << m_cfg.upstream_port << '\n';
}

Server::~Server() {
    if (m_epoll_fd != -1) close(m_epoll_fd);
    if (m_server_fd != -1) close(m_server_fd);
}

void Server::remove_session(std::shared_ptr<Session> sess) {
    if (!sess) return;

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sess->client_addr.sin_addr, ip, sizeof(ip));
    std::cout << "Session " << ip << ':' << ntohs(sess->client_addr.sin_port) << " disconnected\n";

    if (sess->client_fd != -1) {
        epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, sess->client_fd, nullptr);
        m_sessions.erase(sess->client_fd);
        close(sess->client_fd);
        sess->client_fd = -1;
    }
    if (sess->upstream_fd != -1) {
        epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, sess->upstream_fd, nullptr);
        m_sessions.erase(sess->upstream_fd);
        close(sess->upstream_fd);
        sess->upstream_fd = -1;
    }
}

void Server::run() {
    std::vector<epoll_event> events(m_cfg.max_events);
    std::vector<char> scratch(65536);

    while (true) {
        int n = epoll_wait(m_epoll_fd, events.data(), m_cfg.max_events, -1);
        if (n == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev_mask = events[i].events;

            if (fd == m_server_fd) {
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(m_server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);

                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("accept");
                        break;
                    }

                    fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL) | O_NONBLOCK);

                    int up_fd = connect_to_upstream(m_cfg.upstream_host, m_cfg.upstream_port);
                    if (up_fd == -1) {
                        close(client_fd);
                        continue;
                    }

                    auto sess = std::make_shared<Session>();
                    sess->client_fd = client_fd;
                    sess->upstream_fd = up_fd;
                    sess->client_addr = client_addr;

                    sess->client_to_upstream.want_write = true;

                    m_sessions[client_fd] = sess;
                    m_sessions[up_fd] = sess;

                    epoll_event cev{};
                    cev.events = EPOLLIN;
                    cev.data.fd = client_fd;
                    epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, client_fd, &cev);

                    epoll_event uev{};
                    uev.events = EPOLLIN | EPOLLOUT;
                    uev.data.fd = up_fd;
                    epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, up_fd, &uev);

                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
                    std::cout << "Session established for client " << ip << ':' << ntohs(client_addr.sin_port) << '\n';
                }
                continue;
            }

            auto it = m_sessions.find(fd);
            if (it == m_sessions.end()) continue;
            auto sess = it->second;

            bool is_client = (fd == sess->client_fd);

            StreamState &in_state = is_client ? sess->client_to_upstream : sess->upstream_to_client;
            StreamState &out_state = is_client ? sess->upstream_to_client : sess->client_to_upstream;
            int peer_fd = is_client ? sess->upstream_fd : sess->client_fd;

            bool alive = true;

            if (ev_mask & EPOLLIN) {
                if (!handle_read(fd, in_state, m_cfg.buffer_size, scratch)) {
                    alive = false;
                }
            }

            if (alive && in_state.buf.size() > in_state.offset) {
                if (!try_write(peer_fd, in_state)) {
                    alive = false;
                }
                if (!in_state.want_read && (in_state.buf.size() - in_state.offset) < m_cfg.buffer_size) {
                    in_state.want_read = true;
                }
            }

            if (alive) {
                check_write_shutdown(peer_fd, in_state);
            }

            if (alive && (ev_mask & EPOLLOUT)) {
                if (!try_write(fd, out_state)) {
                    alive = false;
                }

                if (alive && (out_state.buf.size() - out_state.offset) < m_cfg.buffer_size && !out_state.want_read) {
                    out_state.want_read = true;
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
                update_epoll(m_epoll_fd, fd, in_state.want_read, out_state.want_write);
                update_epoll(m_epoll_fd, peer_fd, out_state.want_read, in_state.want_write);
            }
        }
    }
}
