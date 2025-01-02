# Samples

`t1.c` is the absolute minimal core of a X11 Window Manager which provided exactly the following features:

1. Open 1 window at a time of specific size of possible 3 applications.
2. Specifically:
  - `Alt-f` opens Firefox
  - `Alt-c` opens Chrome
  - `Alt-t` opens XTerm
3. Close the currently open window.

`t1.c` and `t2.c` are functionally identical excepting one small thing - but `t2` crashes.

## Compiling

```
cc -lX11 t1.c -o t1
cc -lX11 -lpthread t2.c -o t2
```

## The Crash

Though I later found the exact reason for the crash (not properly closing the connection to the X11 display when forking to run an application), in general it seems that X11 does not properly play with multi-threading, though it does explicitly support it.

What helped me debug and solve the problem in the main wm was `valgrind` as follows:
```
valgrind --tool=callgrind <program>
```
