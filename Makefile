LDLIBS += -lX11 -lXft -lpthread \
	`pkg-config --libs fontconfig`

CFLAGS += -g -std=c99 -Wall -Wextra \
	`pkg-config --cflags x11` \
	`pkg-config --cflags fontconfig` \

.PHONY: all clean

all: wm

wm: wm.c
	gcc $(CFLAGS) $(LDLIBS) -o wm wm.c

clean:
	rm wm
