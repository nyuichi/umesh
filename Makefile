CC = gcc
CFLAGS = -Wall -g
SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

TARGET = ish

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	$(RM) $(TARGET) $(OBJS) *~
