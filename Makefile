
PKGS       := ncursesw
CC         := gcc
PKG_CONFIG ?= pkg-config
CFLAGS     := -W -Wall -g -O3 $(shell $(PKG_CONFIG) --cflags $(PKGS))
LDFLAGS    := $(shell $(PKG_CONFIG) --libs $(PKGS))
AS         := as
ASFLAGS    := -gdbb --32
PROGS      := camera-ctl

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
