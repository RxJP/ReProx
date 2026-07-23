// Minimal threaded TCP echo backend for benchmarking (latency + connection rate tests) with a deliberately simple thread-per-connection.

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

static void handle_connection(int client_fd) {
    char buf[65536];
    while (true) {
        ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
        if (n <= 0) break;

        ssize_t sent = 0;
        while (sent < n) {
            ssize_t w = send(client_fd, buf + sent, static_cast<size_t>(n - sent), 0);
            if (w <= 0) {
                close(client_fd);
                return;
            }
            sent += w;
        }
    }
    close(client_fd);
}

int main(int argc, char **argv) {
    int port = 10001;
    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--port") == 0) {
            port = std::atoi(argv[i + 1]);
        }
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(listen_fd, 1024) < 0) {
        perror("listen");
        return 1;
    }

    std::printf("echo_backend listening on port %d\n", port);
    std::fflush(stdout);

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        std::thread(handle_connection, client_fd).detach();
    }
}