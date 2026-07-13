#include "config.hpp"

#include <stdexcept>
#include <toml++/toml.hpp>

Config load_config(const std::string &path) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error &e) {
        throw std::runtime_error(std::string("Failed to parse config: ") + e.what());
    }

    Config cfg;

    if (auto proxy = tbl["proxy"].as_table()) {
        if (auto v = (*proxy)["port"].value<int>()) cfg.port = *v;
        if (auto v = (*proxy)["backlog"].value<int>()) cfg.backlog = *v;
        if (auto v = (*proxy)["buffer_size"].value<int>()) cfg.buffer_size = *v;
        if (auto v = (*proxy)["upstream_host"].value<std::string>()) cfg.upstream_host = *v;
        if (auto v = (*proxy)["upstream_port"].value<int>()) cfg.upstream_port = *v;
    }

    if (auto ep = tbl["epoll"].as_table()) {
        if (auto v = (*ep)["max_events"].value<int>()) cfg.max_events = *v;
    }

    return cfg;
}
