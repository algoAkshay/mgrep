CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread -Iinclude
TARGET   = mgrep
SRCDIR   = src
INCDIR   = include
SRCS     = $(SRCDIR)/main.cpp $(SRCDIR)/searcher.cpp
OBJS     = $(SRCS:.cpp=.o)
DEPS     = $(OBJS:.o=.d)
PYTHON  ?= python3

# debug build - adds sanitizers, slower but catches bugs
# usage: make debug
DEBUG_FLAGS = -g -O0 -fsanitize=address -fno-omit-frame-pointer

.PHONY: all clean debug test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(SRCDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

-include $(DEPS)

debug:
	$(CXX) $(CXXFLAGS) $(DEBUG_FLAGS) -o $(TARGET)_debug $(SRCS)

test: $(TARGET)
	$(CXX) $(CXXFLAGS) -o tests/thread_pool_test tests/thread_pool_test.cpp
	./tests/thread_pool_test
	$(CXX) $(CXXFLAGS) -o tests/searcher_test tests/searcher_test.cpp src/searcher.cpp
	./tests/searcher_test
	$(PYTHON) tests/test_cli.py ./$(TARGET)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET) $(TARGET)_debug $(TARGET).exe tests/thread_pool_test tests/thread_pool_test.exe tests/searcher_test tests/searcher_test.exe
