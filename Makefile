CXX ?= c++
CXXFLAGS ?= -O3 -mcpu=native -std=c++17 -Wall -Wextra

.PHONY: all run test bench clean

all: build/eval build/test build/bench

build:
	mkdir -p build

build/eval: main.cpp evaluator.hpp | build
	$(CXX) $(CXXFLAGS) main.cpp -o $@

build/test: test_exhaustive.cpp evaluator.hpp | build
	$(CXX) $(CXXFLAGS) test_exhaustive.cpp -o $@

build/bench: bench.cpp evaluator.hpp | build
	$(CXX) $(CXXFLAGS) bench.cpp -o $@

run: build/eval
	./build/eval

test: build/test
	./build/test

bench: build/bench
	./build/bench

clean:
	rm -rf build
