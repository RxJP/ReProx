#include "utils.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>

int connect_to_upstream(const std::string &host, int port) {
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

void update_epoll(int epoll_fd, int fd, bool want_read, bool want_write) {
    epoll_event ev{};
    if (want_read) ev.events |= EPOLLIN;
    if (want_write) ev.events |= EPOLLOUT;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        perror("epoll_ctl mod"); // Can happen if FD is already closed/invalid
    }
}

bool handle_read(int src_fd, StreamState &out_state, size_t max_buf_size, std::vector<char> &scratch) {
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

bool try_write(int dst_fd, StreamState &state) {
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

void check_write_shutdown(int dst_fd, StreamState &state) {
    if (state.closed && state.buf.size() == state.offset && !state.write_shutdown) {
        shutdown(dst_fd, SHUT_WR);
        state.write_shutdown = true;
        state.want_write = false;
    }
}
