CC = gcc
obj-m := fifo.o
PWD := $(shell pwd)

default: 
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

test: test.c
	$(CC) -o $@ $@.c

clean: 
	rm -f test
	rm -rf ./.tmp_versions
	rm -f $(wildcard ./*~)
	rm -f $(wildcard ./*.o)
	rm -f $(wildcard ./*mod*)
	rm -f $(wildcard ./*.ko)
	rm -f $(wildcard ./Module.symvers)
	rm -f $(wildcard ./.*fifo*)
