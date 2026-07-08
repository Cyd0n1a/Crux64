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
    $(BUILD_DIR)/src/audio/synth.o \
    $(BUILD_DIR)/src/audio/music.o \
    $(BUILD_DIR)/src/audio/minimp3.o \
    $(BUILD_DIR)/src/gen/noise.o \
    $(BUILD_DIR)/src/gen/mountain.o \
    $(BUILD_DIR)/src/gen/grips.o \
    $(BUILD_DIR)/src/gen/scatter.o \
    $(BUILD_DIR)/src/gen/proctree.o \
    $(BUILD_DIR)/src/gen/tree_gen.o \
    $(BUILD_DIR)/src/sim/climber.o \
    $(BUILD_DIR)/src/sim/campsite.o \
    $(BUILD_DIR)/src/meta/save.o \
    $(BUILD_DIR)/src/meta/dialogue.o \
    $(BUILD_DIR)/src/meta/prologue.o \
    $(BUILD_DIR)/src/render/render.o \
    $(BUILD_DIR)/src/render/climber_render.o \
    $(BUILD_DIR)/src/render/campsite_render.o \
    $(BUILD_DIR)/src/render/scatter_render.o \
    $(BUILD_DIR)/src/render/title_render.o

# Background music (GDD 3.3): the MP3s under assets/ are copied verbatim into
# the ROM filesystem and streamed/decoded at runtime by minimp3 (no audioconv
# step — they stay compressed on the cart). The in-game track is larger than
# RAM, so it must live in the DFS image, not be linked in.
assets_mp3  = $(wildcard assets/*.mp3)
assets_conv = $(addprefix filesystem/,$(notdir $(assets_mp3)))

filesystem/%.mp3: assets/%.mp3
	@mkdir -p $(dir $@)
	@cp $< $@

# minimp3 is vendored third-party code; don't hold it to our -Werror.
$(BUILD_DIR)/src/audio/minimp3.o: CFLAGS += -Wno-error -w

# proctree (Paul Brunt / Jari Komppa, 3-clause BSD) is vendored too.
$(BUILD_DIR)/src/gen/proctree.o: CXXFLAGS += -Wno-error -w

crux64.z64: $(BUILD_DIR)/crux64.elf
crux64.z64: $(BUILD_DIR)/crux64.dfs
$(BUILD_DIR)/crux64.dfs: $(assets_conv)
$(BUILD_DIR)/crux64.elf: $(OBJS)

clean:
	rm -rf $(BUILD_DIR) filesystem crux64.z64 crux64.elf.sym

-include $(OBJS:.o=.d)

.PHONY: all clean
