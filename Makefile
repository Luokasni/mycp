CC       = gcc 
CXX      = g++
CFLAGS   = -Wall -Wextra -std=gnu11 -O2 -pthread
CXXFLAGS = -Wall -Wextra -std=c++17 -O2 -pthread
LDFLAGS  = -pthread

# the C unit tests inject failures (read/write/malloc/...) through the linker
WRAP     = -Wl,--wrap=malloc -Wl,--wrap=strdup -Wl,--wrap=read -Wl,--wrap=write \
	       -Wl,--wrap=pread -Wl,--wrap=pwrite -Wl,--wrap=lseek -Wl,--wrap=ftruncate \
	       -Wl,--wrap=fchmod -Wl,--wrap=close -Wl,--wrap=pthread_create

LIB_OBJS = copy.o parallel.o dir.o queue.o utils.o log.o
OBJS     = main.o $(LIB_OBJS)
TARGET   = mycp
TEST_C   = tests/test_mycp
TEST_GT  = tests/gtest_mycp

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c mycp.h
	$(CC) $(CFLAGS) -c $< -o $@

# ---- tests ------------------------------------------------------------
# plain C unit tests
$(TEST_C): tests/test_mycp.c $(LIB_OBJS) mycp.h
	$(CC) $(CFLAGS) -I. -o $@ tests/test_mycp.c $(LIB_OBJS) $(WRAP) $(LDFLAGS)

# Google Test (needs: sudo apt-get install -y libgtest-dev)
$(TEST_GT): tests/gtest_mycp.cpp $(LIB_OBJS) mycp.h
	$(CXX) $(CXXFLAGS) -I. -o $@ tests/gtest_mycp.cpp $(LIB_OBJS) -lgtest_main -lgtest $(LDFLAGS)

# C unit tests + command-line tests
test: $(TARGET) $(TEST_C)
	./$(TEST_C)
	bash tests/test_cli.sh

# Google Test suite
gtest: $(TEST_GT)
	./$(TEST_GT)

# memory-leak / memory-error check with Valgrind (needs: sudo apt-get install -y valgrind)
valgrind: $(TARGET) $(TEST_C)
	bash tests/run_valgrind.sh

# code coverage with gcov/lcov (needs: sudo apt-get install -y lcov libgtest-dev)
coverage:
	bash tests/run_coverage.sh

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_C) $(TEST_GT)
	rm -rf cov
	rm -f *.gcda *.gcno *.gcov

.PHONY: all test gtest valgrind coverage clean
