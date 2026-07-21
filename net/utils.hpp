#ifndef REPROX_NET_UTILS_HPP
#define REPROX_NET_UTILS_HPP

#include <string>
#include <vector>
#include "../session/session.hpp"

// Opens a non-blocking TCP connection to the upstream server.
// Returns the connecting fd, or -1 on failure.
int connect_to_upstream(const std::string &host, int port);

void update_epoll(int epoll_fd, int fd, bool want_read, bool want_write);

// Reads from `src_fd` into `out_state.buf`.
bool handle_read(int src_fd, StreamState &out_state, size_t max_buf_size, std::vector<char> &scratch);

// Attempts to send data from `state.buf` out to `dst_fd`.
// Sets `state.want_write` accordingly.
bool try_write(int dst_fd, StreamState &state);

void check_write_shutdown(int dst_fd, StreamState &state);

#endif // REPROX_NET_UTILS_HPP
