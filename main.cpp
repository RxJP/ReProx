#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "config.hpp"

int main() {
    Config cfg;
    try {
        cfg = load_config("./../config.toml");
    } catch (const std::exception& e) {
        std::cerr << "Config error: " << e.what() << '\n';
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // Allow quick restart

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(cfg.port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
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

    std::cout << "Listening on port " << cfg.port << '\n';

    std::unordered_map<int, sockaddr_in> client_addrs;

    std::vector<epoll_event> events(cfg.max_events);
    std::vector<char> buffer(cfg.buffer_size);

    while (true) {
        int n = epoll_wait(epoll_fd, events.data(), cfg.max_events, -1);
        if (n == -1) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == server_fd) {
                // Accept all pending connections
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("accept");
                        break;
                    }

                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
                    std::cout << "Client connected: " << ip << ':' << ntohs(client_addr.sin_port) << '\n';

                    // Register client fd with epoll
                    epoll_event cev{};
                    cev.events = EPOLLIN | EPOLLET;
                    cev.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &cev) == -1) {
                        perror("epoll_ctl: client_fd");
                        close(client_fd);
                        continue;
                    }

                    client_addrs[client_fd] = client_addr;
                }
            } else {
                // Data available on a client fd
                ssize_t bytes = recv(fd, buffer.data(), buffer.size(), 0);

                if (bytes == 0) {
                    auto& client_addr = client_addrs[fd];
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
                    std::cout << "Client " << ip << ':' << ntohs(client_addr.sin_port) << " disconnected\n";
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                    client_addrs.erase(fd);
                } else if (bytes < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                        perror("recv");
                } else {
                    std::cout << "Received: " << std::string_view(buffer.data(), bytes) << '\n';
                    send(fd, buffer.data(), bytes, 0);
                }
            }
        }
    }

    close(epoll_fd);
    close(server_fd);
}
