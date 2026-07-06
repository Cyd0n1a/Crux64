BUILD_DIR = build

include $(N64_INST)/include/n64.mk
include tiny3d/t3d.mk

N64_ROM_TITLE = "CRUX64"
N64_ROM_SAVETYPE = eeprom4k

all: crux64.z64

OBJS = \
    $(BUILD_DIR)/src/main.o \
    $(BUILD_DIR)/src/input/input.o \
    $(BUILD_DIR)/src/input/rumble.o \
    $(BUILD_DIR)/src/gen/noise.o \
    $(BUILD_DIR)/src/gen/mountain.o \
    $(BUILD_DIR)/src/gen/grips.o \
    $(BUILD_DIR)/src/sim/climber.o \
    $(BUILD_DIR)/src/sim/campsite.o \
    $(BUILD_DIR)/src/render/render.o \
    $(BUILD_DIR)/src/render/climber_render.o \
    $(BUILD_DIR)/src/render/campsite_render.o

crux64.z64: $(BUILD_DIR)/crux64.elf
$(BUILD_DIR)/crux64.elf: $(OBJS)

clean:
	rm -rf $(BUILD_DIR) filesystem crux64.z64 crux64.elf.sym

-include $(OBJS:.o=.d)

.PHONY: all clean
