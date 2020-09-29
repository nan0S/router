CC = gcc
CFLAGS = -Wall -Wextra
DEPS = entry.h
OBJ = router.o

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

router: $(OBJ)
	gcc $(CFLAGS) -o $@ $^

clean:
	rm -f *.o
cleandist:
	rm -f *.o router