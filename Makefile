ARCH   ?= $(shell uname -m)
TARGET ?= macosx



CFLAGS  := -arch $(ARCH) -Os -Wall -Wshadow

CFLAGS += $(if $(DEBUG),-g -fsanitize=address)
CFLAGS += $(if $(COMPACT),-DCOMPACT)
PREPFLAGS := $(CFLAGS)

ifdef MIN_OSVER
	ifeq ($(TARGET), macosx)
		export MACOSX_DEPLOYMENT_TARGET=$(MIN_OSVER)
	endif
	ifeq ($(TARGET), iphoneos)
		export IPHONEOS_DEPLOYMENT_TARGET=$(MIN_OSVER)
	endif
endif

ifeq ($(TARGET), iphoneos)
	CFLAGS += -isysroot $(shell xcrun --sdk $(TARGET) --show-sdk-path)
endif

all: libstickhook.a stickprep

libstickhook.a: src/stickhook.o
	ar -rcs $@ $^

stickprep: src/stickprep.c
	$(CC) $(PREPFLAGS) -o $@ $^

clean:
	rm -f src/stickhook.o
	rm -f libstickhook.a stickprep

.PHONY: clean all
