GCC = gcc
CFLAGS = -g -Wall -Wshadow
LOCFLAGS = -lm -lpthread
SRC = $(wildcard *.c)
TAR = $(SRC:.c=.o)
EXEC = $(SRC:.c=)
LOGS = $(wildcard *.log)

.PHONY: all clean

all: $(TAR)

%.o: %.c
	$(GCC) $(CFLAGS) $(LOCFLAGS) -c $<
	$(GCC) -o $* $@ $(CFLAGS) $(LOCFLAGS)

clean:
	rm -f $(TAR)
	rm -f $(EXEC)
	rm -f $(LOGS)
