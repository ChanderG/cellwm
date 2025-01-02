LDLIBS += -lX11 -lXft \
	`pkg-config --libs fontconfig`

CFLAGS += -g -std=c99 -Wall -Wextra \
	`pkg-config --cflags x11` \
	`pkg-config --cflags fontconfig` \

.PHONY: all clean

all: wm

wm: wm.c
	gcc $(CFLAGS) $(LDLIBS) -o cellwm wm.c

install:
	cp cellwm /usr/local/bin/
	cp cellwm.desktop /usr/share/xsessions/

uinstall:
	rm -rf /usr/local/bin/cellwm

clean:
	rm cellwm
