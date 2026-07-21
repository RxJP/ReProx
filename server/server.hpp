#ifndef REPROX_SERVER_HPP
#define REPROX_SERVER_HPP

#include <memory>
#include <unordered_map>
#include "../config/config.hpp"
#include "../session/session.hpp"

class Server {
public:
    explicit Server(const Config &cfg);

    ~Server();

    void run();

private:
    void remove_session(std::shared_ptr<Session> sess);

    Config m_cfg;
    int m_server_fd = -1;
    int m_epoll_fd = -1;
    std::unordered_map<int, std::shared_ptr<Session> > m_sessions;
};

#endif // REPROX_SERVER_HPP