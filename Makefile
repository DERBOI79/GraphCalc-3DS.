# GraphCalc 3DS - simple Makefile for devkitPro (devkitARM + libctru)
#
#   export DEVKITPRO=/opt/devkitpro
#   make            -> graphcalc.3dsx (copy to /3ds/ on the SD card)
#   make clean

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM

CC      := $(DEVKITARM)/bin/arm-none-eabi-gcc
SMDH    := $(DEVKITPRO)/tools/bin/smdhtool
DSXTOOL := $(DEVKITPRO)/tools/bin/3dsxtool
LIBCTRU := $(DEVKITPRO)/libctru

TARGET  := graphcalc
SRC     := source/main.c source/calc.c
OBJ     := $(SRC:source/%.c=build/%.o)

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) \
           -D__3DS__ -I$(LIBCTRU)/include -Isource
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections -L$(LIBCTRU)/lib
LIBS    := -lctru -lm

all: $(TARGET).3dsx

build:
	mkdir -p build

build/%.o: source/%.c source/calc.h source/font.h | build
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

$(TARGET).smdh:
	$(SMDH) --create "GraphCalc" "Desmos-style graphing calculator" "GraphCalc" \
	    $(LIBCTRU)/default_icon.png $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	$(DSXTOOL) $(TARGET).elf $@ --smdh=$(TARGET).smdh

clean:
	rm -rf build $(TARGET).elf $(TARGET).smdh $(TARGET).3dsx

.PHONY: all clean
