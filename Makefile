CFLAGS := -D_GNU_SOURCE

all: xperable

xperable: xperable.c
	gcc $(CFLAGS) -o xperable.o -c xperable.c -Ilibusb-1.0/libusb.h
	g++ $(CFLAGS) -Lx86_64-linux-gnu/libusb-1.0.a -o xperable xperable.o -lusb-1.0
