# LPS Companion MS-DOS — Open Watcom (-0 8086) + host gcc tests
WATCOM ?= /workspace/lps/tools/watcom
export WATCOM
export INCLUDE := $(WATCOM)/h
export LIB := $(WATCOM)/lib286

WCC = $(WATCOM)/binl64/wcc
WCL = $(WATCOM)/binl64/wcl
WLINK = $(WATCOM)/binl64/wlink

DOS_CFLAGS = -0 -bt=dos -ms -zq -ox -dLPS_DOS -i=src
HOST_CC = gcc
HOST_CFLAGS = -std=c89 -Wall -Wextra -Wno-unused-parameter -O2 -Isrc -DLPS_HOST

SRC_COMMON = src/state.c src/ics.c src/save.c src/paths.c src/rosary.c src/app.c
DOS_OBJS = $(SRC_COMMON) src/plat_dos.c src/main.c
HOST_OBJS = $(SRC_COMMON) src/plat_host.c src/main.c

.PHONY: all dos host test clean captures

all: dos host test

dos: LPSCOMP.EXE

LPSCOMP.EXE: $(DOS_OBJS) src/lps.h src/rosary.h
	$(WCL) $(DOS_CFLAGS) -fe=LPSCOMP.EXE $(DOS_OBJS)

host: lpscomp-host

lpscomp-host: $(HOST_OBJS) src/lps.h src/rosary.h
	$(HOST_CC) $(HOST_CFLAGS) -o lpscomp-host $(HOST_OBJS)

test: tests/test_storage
	./tests/test_storage

tests/test_storage: tests/test_storage.c $(SRC_COMMON) src/plat_host.c src/lps.h
	$(HOST_CC) $(HOST_CFLAGS) -o tests/test_storage tests/test_storage.c $(SRC_COMMON) src/plat_host.c

captures: host
	mkdir -p captures
	rm -rf /tmp/lps-cap && mkdir -p /tmp/lps-cap
	LPSDIR=/tmp/lps-cap ./tools/drive_captures.sh

clean:
	rm -f LPSCOMP.EXE lpscomp-host tests/test_storage *.o src/*.o tests/*.o *.err
