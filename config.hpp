#pragma once

#include <string>

struct Config {
    // [proxy]
    int         port        = 8700;
    int         backlog     = 128;
    int         buffer_size = 10240;

    // [epoll]
    int         max_events  = 64;
};

// Loads config from the given TOML file path.
// Throws std::runtime_error on file-not-found or parse errors.
Config load_config(const std::string& path);
