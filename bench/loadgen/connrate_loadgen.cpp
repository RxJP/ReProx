/*Measures sustained connection establishment rate: repeatedly connects,
exchanges a single-byte round trip (to exercise the full accept +
upstream-connect + session-setup path through the proxy), then closes. Runs N concurrent worker threads for
a fixed duration and reports the aggregate connections/sec.*/

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <vector>
#include <unistd.h>

using Clock = std::chrono::steady_clock;

struct Options {
    std::string host = "127.0.0.1";
    int port = 8700;
    int workers = 8;
    double duration_s = 10.0;
};

static Options parse_args(int argc, char **argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--host") o.host = next();
        else if (a == "--port") o.port = std::atoi(next().c_str());
        else if (a == "--workers") o.workers = std::atoi(next().c_str());
        else if (a == "--duration") o.duration_s = std::atof(next().c_str());
    }
    return o;
}

struct WorkerResult {
    long succeeded = 0;
    long failed = 0;
};

static void worker(const Options &opt, WorkerResult &result) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(opt.port));
    inet_pton(AF_INET, opt.host.c_str(), &addr.sin_addr);

    auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(opt.duration_s));

    char one_byte = 'x';
    char resp;

    while (Clock::now() < deadline) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            result.failed++;
            continue;
        }

        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            result.failed++;
            close(fd);
            continue;
        }
        if (send(fd, &one_byte, 1, 0) != 1 || recv(fd, &resp, 1, 0) != 1) {
            result.failed++;
            close(fd);
            continue;
        }
        close(fd);
        result.succeeded++;
    }
}

int main(int argc, char **argv) {
    Options opt = parse_args(argc, argv);

    std::vector<WorkerResult> results(static_cast<size_t>(opt.workers));
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(opt.workers));

    auto t0 = Clock::now();
    for (int i = 0; i < opt.workers; ++i) {
        threads.emplace_back(worker, std::cref(opt), std::ref(results[static_cast<size_t>(i)]));
    }
    for (auto &t: threads) t.join();
    auto t1 = Clock::now();

    long total_succeeded = 0, total_failed = 0;
    for (auto &r: results) {
        total_succeeded += r.succeeded;
        total_failed += r.failed;
    }

    double actual_duration_s = std::chrono::duration<double>(t1 - t0).count();
    double conn_per_sec = actual_duration_s > 0
                              ? static_cast<double>(total_succeeded) / actual_duration_s
                              : 0.0;

    std::printf("{\n");
    std::printf("  \"workers\": %d,\n", opt.workers);
    std::printf("  \"duration_s\": %.3f,\n", actual_duration_s);
    std::printf("  \"succeeded\": %ld,\n", total_succeeded);
    std::printf("  \"failed\": %ld,\n", total_failed);
    std::printf("  \"connections_per_sec\": %.2f\n", conn_per_sec);
    std::printf("}\n");

    return 0;
}
