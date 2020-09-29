TARGET = router
EXENAME = router

CC = gcc
CFLAGS = -Wall -Wextra
DEPS = entry.h
OBJS = $(TARGET).o

all: $(TARGET)

install: $(TARGET) clean

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(EXENAME) $^

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf *.o
distclean: clean
	rm -f $(EXENAME)

.PHONY: clean
