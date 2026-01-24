CFLAGS := -D_GNU_SOURCE

all: xperable

xperable.o: xperable.c
	g++ $(CFLAGS) -Lx86_64-linux-gnu/libusb-1.0.a -o xperable.o xperable -Ilibusb-1.0/libusb.h
	gcc $(CFLAGS) -o $@ -c $<
