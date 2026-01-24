CFLAGS := -D_GNU_SOURCE -ggdb

XPERABLE ?= xperable
CC := gcc
CXX := g++

all: $(XPERABLE)

xperable.o: xperable.c
	$(CC) $(CFLAGS) -o $@ -c $<

$(XPERABLE): xperable.o
	$(CXX) $(CFLAGS) -static -Llibusb-static -o $@ $^ -lusb-1.0
