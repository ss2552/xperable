CFLAGS := -D_GNU_SOURCE -ggdb

all: xperable

xperable.o: xperable.c
	gcc $(CFLAGS) -o $@ -c $<

xperable: xperable.o
	g++ $(CFLAGS) -Llibusb -o $@ $^ -lusb-1.0
