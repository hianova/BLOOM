CC ?= clang
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Wmissing-prototypes -Werror -O2
test: test_swept
	./test_swept
test_swept: swept.c swept.h test_swept.c
	$(CC) $(CFLAGS) swept.c test_swept.c -o test_swept -lm
clean:
	rm -f test_swept
