CC      = gcc
CFLAGS  = -Wall -Wextra -std=gnu11 -O2
OBJS    = main.o copy.o dir.o utils.o log.o
TARGET  = mycp

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c mycp.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
