# GraphCalc 3DS - builds both Homebrew Launcher .3dsx and installable .cia

DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM

CC      := $(DEVKITARM)/bin/arm-none-eabi-gcc
SMDH    := $(DEVKITPRO)/tools/bin/smdhtool
DSXTOOL := $(DEVKITPRO)/tools/bin/3dsxtool
MAKEROM ?= $(shell command -v makerom 2>/dev/null)
LIBCTRU := $(DEVKITPRO)/libctru

TARGET  := graphcalc
SRC     := source/main.c source/calc.c
OBJ     := $(SRC:source/%.c=build/%.o)

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) \
           -D__3DS__ -I$(LIBCTRU)/include -Isource
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,--gc-sections -L$(LIBCTRU)/lib
LIBS    := -lctru -lm

all: $(TARGET).3dsx $(TARGET).cia

build:
	mkdir -p build

build/%.o: source/%.c source/calc.h source/font.h | build
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

$(TARGET).smdh: assets/icon.png
	$(SMDH) --create "GraphCalc" "Graphing calculator" "GraphCalc" $< $@

$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	$(DSXTOOL) $(TARGET).elf $@ --smdh=$(TARGET).smdh

$(TARGET).cia: $(TARGET).elf $(TARGET).smdh project.rsf
	@test -n "$(MAKEROM)" || (echo "makerom not found"; exit 1)
	$(MAKEROM) -f cia -o $@ -DAPP_ENCRYPTED=false -rsf project.rsf \
		-target t -exefslogo -elf $(TARGET).elf -icon $(TARGET).smdh

clean:
	rm -rf build $(TARGET).elf $(TARGET).smdh $(TARGET).3dsx $(TARGET).cia

.PHONY: all clean
