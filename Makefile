LDLIBS += -lX11

CFLAGS += -g -std=c99 -Wall -Wextra \
	`pkg-config --cflags x11` \

.PHONY: all clean

all: wm

wm: wm.c
	gcc $(CFLAGS) $(LDLIBS) -o wm wm.c

clean:
	rm wm
