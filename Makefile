CXX ?= g++
CXXFLAGS ?= -O3 -std=c++20 -DNDEBUG -Wall -Wextra -Wpedantic -g
TARGET := binary_search_bench
SOURCES := main.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

clean:
	rm -f $(TARGET)