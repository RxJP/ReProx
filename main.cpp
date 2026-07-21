#include <iostream>
#include "config/config.hpp"
#include "server/server.hpp"

int main() {
    Config cfg;
    try {
        cfg = load_config("./../config.toml");
    } catch (const std::exception &e) {
        std::cerr << "Config error: " << e.what() << '\n';
        return 1;
    }

    Server server(cfg);
    server.run();

    return 0;
}
