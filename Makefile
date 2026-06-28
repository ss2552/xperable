CFLAGS := -D_GNU_SOURCE

all: xperable_aarch64

xperable_x86_64: xperable.c
	gcc $(CFLAGS) -o xperable.o -c xperable.c -Ilibusb-1.0/libusb.h
	g++ $(CFLAGS) -Lx86_64-linux-gnu/libusb-1.0.a -o xperable xperable.o -lusb-1.0


xperable_aarch64: xperable.c
	gcc $(CFLAGS) -I/usr/include/libusb-1.0 -c xperable.c -o xperable.o
	g++ $(CFLAGS) -L/usr/lib/aarch64-linux-gnu -o $@ xperable.o -lusb-1.0