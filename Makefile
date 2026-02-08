LIBRETRO_MAKEFILE ?= Makefile.libretro
STANDALONE_MAKEFILE ?= Makefile.standalone
MINGW_PREFIX ?= x86_64-w64-mingw32
MINGW_CC ?= $(MINGW_PREFIX)-gcc
MINGW_CXX ?= $(MINGW_PREFIX)-g++
MINGW_AS ?= $(MINGW_PREFIX)-as
MINGW_AR ?= $(MINGW_PREFIX)-ar
MINGW_STRIP ?= $(MINGW_PREFIX)-strip

.PHONY: all clean standalone standalone-clean w64 help

all:
	$(MAKE) -f $(LIBRETRO_MAKEFILE)

clean:
	$(MAKE) -f $(LIBRETRO_MAKEFILE) clean

standalone:
	$(MAKE) -f $(STANDALONE_MAKEFILE)

standalone-clean:
	$(MAKE) -f $(STANDALONE_MAKEFILE) clean

w64:
	$(MAKE) -C cpu/musashi clean
	$(MAKE) -C cpu/musashi CC=cc
	$(MAKE) -f $(LIBRETRO_MAKEFILE) \
		platform=win64 \
		CC=$(MINGW_CC) \
		CXX=$(MINGW_CXX) \
		AS=$(MINGW_AS) \
		AR=$(MINGW_AR) \
		STRIP=$(MINGW_STRIP) \
		E9K_MEGADRIVE_CORE_DST=../e9k-debugger/system/mega9000.dll

help:
	@echo "Default build: libretro core"
	@echo "  make             -> uses $(LIBRETRO_MAKEFILE)"
	@echo "  make clean       -> cleans libretro build artifacts"
	@echo "  make w64         -> cross-builds win64 libretro core to ../e9k-debugger/system/mega9000.dll"
	@echo "  make standalone      -> runs standalone top-level build"
	@echo "  make standalone-clean-> cleans standalone top-level build"
