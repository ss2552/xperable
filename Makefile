CFLAGS := -D_GNU_SOURCE

.PHONY: xperable_x86_64 xperable_aarch64

all: xperable_x86_64 xperable_aarch64

xperable_x86_64: xperable.c
    @echo "build xperable_x86_64"
	@gcc $(CFLAGS) -o xperable.o -c xperable.c -Ilibusb-1.0/libusb.h
	@g++ $(CFLAGS) -Lx86_64-linux-gnu/libusb-1.0.a -o xperable xperable.o -lusb-1.0

xperable_aarch64: xperable.c
    @echo "build xperable_aarch64"
    @export ANDROID_TERMUX=1
    @echo $(ANDROID_TERMUX)
    @clang -o ../x xperable.c -lusb-1.0
