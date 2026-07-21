#ifndef REPROX_SESSION_HPP
#define REPROX_SESSION_HPP

#include <vector>
#include <netinet/in.h>
#include <unistd.h>

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

#endif // REPROX_SESSION_HPP
