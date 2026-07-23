# bench/Makefile — builds the benchmarking tools (echo backend + load
# generators). These are standalone binaries separate from the main project's build.

# Usage:
#   make            # builds everything into bench/bin/
#   make clean

CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pthread

BIN_DIR := bin

.PHONY: all clean

all: $(BIN_DIR)/echo_backend $(BIN_DIR)/latency_loadgen $(BIN_DIR)/connrate_loadgen

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/echo_backend: backend/echo_backend.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BIN_DIR)/latency_loadgen: loadgen/latency_loadgen.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(BIN_DIR)/connrate_loadgen: loadgen/connrate_loadgen.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -rf $(BIN_DIR)
