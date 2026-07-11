#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

int main() {
    constexpr int PROXY_PORT = 8700;

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
    addr.sin_port = htons(PROXY_PORT);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) == -1) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    std::cout << "Listening on port " << PROXY_PORT << '\n';

    std::vector<std::pair<int, sockaddr_in>> client_fds;

    while (true) {
        while (true) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);

            int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

            if (client_fd == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                perror("accept");
                continue;
            }

            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));

            std::cout << "Client connected: " << ip << ':' << ntohs(client_addr.sin_port) << '\n';
            client_fds.push_back(std::make_pair<int, sockaddr_in>(std::move(client_fd), std::move(client_addr)));
        }

        char buffer[10240];

        for (int i = 0; i < client_fds.size(); ++i) {
            auto& client = client_fds[i];
            ssize_t bytes = recv(client.first, buffer, sizeof(buffer), MSG_DONTWAIT);

            if (bytes == 0) {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client.second.sin_addr, ip, sizeof(ip));
                std::cout << "Client " << ip << ':' << client.second.sin_port << " disconnected" << std::endl;
                close(client.first);
                std::swap(client_fds[i], client_fds[client_fds.size() - 1]);
                client_fds.pop_back();
                i--;
                continue;
            }
            else if (bytes < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    perror("recv");
                continue;
            }

            std::cout << "Received: "
                      << std::string_view(buffer, bytes)
                      << std::endl;

            send(client.first, buffer, bytes, 0);
        }
    }

    close(server_fd);
}
