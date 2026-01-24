CFLAGS := -D_GNU_SOURCE

all: xperable

xperable: xperable.c
	gcc $(CFLAGS) -o xperable.o -c xperable.c
	g++ $(CFLAGS) -Lx86_64-linux-gnu/libusb-1.0.a -o xperable xperable.o -Ilibusb/libusb.h -lusb-1.0
