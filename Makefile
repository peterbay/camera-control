
PKGS    := ncursesw
CC      := gcc
CFLAGS  := -Wall -O2 -std=c99 $(shell pkg-config --cflags $(PKGS))
LDFLAGS := $(shell pkg-config --libs $(PKGS))
AS      := as
ASFLAGS := -gdbb --32
PROGS   := camera-ctl

.PHONY: all clean

all: $(PROGS)

clean:
	$(RM) *.o $(PROGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

%.o: %.s
	$(AS) $(ASFLAGS) $^ -o $@

camera-ctl: camera-ctl.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@
