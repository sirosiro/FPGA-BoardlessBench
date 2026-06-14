CC ?= gcc
CFLAGS += -Wall -Wextra -I../../../src/include

all: test_bin mcore_baremetal.elf mcore_baremetal2.elf

test_bin: main.c
	$(CC) $(CFLAGS) -o $@ $<

mcore_baremetal.elf: mcore_baremetal.c
	$(CC) $(CFLAGS) -o $@ $<

mcore_baremetal2.elf: mcore_baremetal2.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f test_bin mcore_baremetal.elf mcore_baremetal2.elf *.o

.PHONY: all clean
