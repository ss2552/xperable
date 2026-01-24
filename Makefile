CFLAGS := -D_GNU_SOURCE

all: xperable

xperable.o: xperable.c
	g++ $(CFLAGS) -Llibusb-1.0.a -o xperable.o xperable -llibusb-1.0/libusb.h
	gcc $(CFLAGS) -o $@ -c $<
