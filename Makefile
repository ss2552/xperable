CFLAGS := -D_GNU_SOURCE

all: xperable

xperable.o: xperable.c
	g++ $(CFLAGS) -Llibusb -o xperable.o xperable -lusb-1.0
	gcc $(CFLAGS) -o $@ -c $<
