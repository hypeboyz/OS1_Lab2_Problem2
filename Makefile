CC = gcc
obj-m := fifo.o
CFLAGS_fifo.o := -DDEBUG
PWD := $(shell pwd)
TESTCFLAGS = -O2 -Wall -Wextra

default:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

test: test.c
	clang -o $@ $(TESTCFLAGS) $@.c -lpthread

clean:
	rm -f test
	rm -rf ./.tmp_versions
	rm -f $(wildcard ./*~)
	rm -f $(wildcard ./*.o)
	rm -f $(wildcard ./*mod*)
	rm -f $(wildcard ./*.ko)
	rm -f $(wildcard ./Module.symvers)
	rm -f $(wildcard ./.*fifo*)
