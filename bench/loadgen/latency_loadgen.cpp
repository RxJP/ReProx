// Ping-pong latency measurement over N concurrent connections. Each
// connection repeatedly sends a small fixed payload and waits for the
// exact same number of bytes echoed back, recording the round-trip time.
// Reports latency percentiles as JSON on stdout.
// TCP_NODELAY is set on every connection.

#include <arpa/inet.h>
#include <algorithm>
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
    int connections = 8;
    double duration_s = 10.0;
    double warmup_s = 1.0;
    int payload_size = 64;
};

static Options parse_args(int argc, char **argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--host") o.host = next();
        else if (a == "--port") o.port = std::atoi(next().c_str());
        else if (a == "--connections") o.connections = std::atoi(next().c_str());
        else if (a == "--duration") o.duration_s = std::atof(next().c_str());
        else if (a == "--warmup") o.warmup_s = std::atof(next().c_str());
        else if (a == "--payload-size") o.payload_size = std::atoi(next().c_str());
    }
    return o;
}

static int connect_to(const std::string &host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool send_all(int fd, const char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        ssize_t w = send(fd, buf + sent, static_cast<size_t>(len - sent), 0);
        if (w <= 0) return false;
        sent += static_cast<int>(w);
    }
    return true;
}

static bool recv_all(int fd, char *buf, int len) {
    int got = 0;
    while (got < len) {
        ssize_t r = recv(fd, buf + got, static_cast<size_t>(len - got), 0);
        if (r <= 0) return false;
        got += static_cast<int>(r);
    }
    return true;
}

static void worker(const Options &opt, std::vector<double> &out_latencies_us) {
    int fd = connect_to(opt.host, opt.port);
    if (fd < 0) return;

    std::vector<char> tx(static_cast<size_t>(opt.payload_size), 'x');
    std::vector<char> rx(static_cast<size_t>(opt.payload_size));

    auto start = Clock::now();
    auto warmup_deadline = start + std::chrono::duration_cast<Clock::duration>(
                               std::chrono::duration<double>(opt.warmup_s));
    auto deadline = start + std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(opt.warmup_s + opt.duration_s));

    // Warm-up phase: exercise the connection without recording samples, so
    // TCP slow start / cache warming doesn't skew the reported numbers.
    while (Clock::now() < warmup_deadline) {
        if (!send_all(fd, tx.data(), opt.payload_size)) {
            close(fd);
            return;
        }
        if (!recv_all(fd, rx.data(), opt.payload_size)) {
            close(fd);
            return;
        }
    }

    while (Clock::now() < deadline) {
        auto t0 = Clock::now();
        if (!send_all(fd, tx.data(), opt.payload_size)) break;
        if (!recv_all(fd, rx.data(), opt.payload_size)) break;
        auto t1 = Clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        out_latencies_us.push_back(us);
    }

    close(fd);
}

static double percentile(const std::vector<double> &sorted, double p) {
    if (sorted.empty()) return 0.0;
    double idx = p * (static_cast<double>(sorted.size()) - 1.0);
    size_t lo = static_cast<size_t>(idx);
    size_t hi = std::min(lo + 1, sorted.size() - 1);
    double frac = idx - static_cast<double>(lo);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
}

int main(int argc, char **argv) {
    Options opt = parse_args(argc, argv);

    std::vector<std::vector<double> > per_thread(static_cast<size_t>(opt.connections));
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(opt.connections));
    for (int i = 0; i < opt.connections; ++i) {
        threads.emplace_back(worker, std::cref(opt), std::ref(per_thread[static_cast<size_t>(i)]));
    }
    for (auto &t: threads) t.join();

    std::vector<double> all;
    for (auto &v: per_thread) all.insert(all.end(), v.begin(), v.end());
    std::sort(all.begin(), all.end());

    if (all.empty()) {
        std::printf("{\"error\": \"no samples collected - could not connect or all requests failed\"}\n");
        return 1;
    }

    double sum = 0.0;
    for (double v: all) sum += v;
    double mean = sum / static_cast<double>(all.size());

    std::printf("{\n");
    std::printf("  \"connections\": %d,\n", opt.connections);
    std::printf("  \"payload_size\": %d,\n", opt.payload_size);
    std::printf("  \"duration_s\": %.2f,\n", opt.duration_s);
    std::printf("  \"sample_count\": %zu,\n", all.size());
    std::printf("  \"latency_us\": {\n");
    std::printf("    \"min\": %.2f,\n", all.front());
    std::printf("    \"mean\": %.2f,\n", mean);
    std::printf("    \"p50\": %.2f,\n", percentile(all, 0.50));
    std::printf("    \"p95\": %.2f,\n", percentile(all, 0.95));
    std::printf("    \"p99\": %.2f,\n", percentile(all, 0.99));
    std::printf("    \"p999\": %.2f,\n", percentile(all, 0.999));
    std::printf("    \"max\": %.2f\n", all.back());
    std::printf("  }\n");
    std::printf("}\n");

    return 0;
}
