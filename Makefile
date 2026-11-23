# Brume - our own HOME menu, built as a qlaunch-ext module (.dnro).
# Run in WSL (archlinux): make BUILD=/root/brume-build   (see scripts/build.sh)

QEXT_DIR  ?= /root/qlaunch-ext
LIBNX     ?= /root/libnx-master-install/opt/devkitpro/libnx
DEVKITPRO ?= /opt/devkitpro
BUILD     ?= build

DLINK := $(QEXT_DIR)/extern/libnx-dlink
NEKO  := $(QEXT_DIR)/extern/SDL-dlink/extern/neko3d
A64   := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
UAM   := $(DEVKITPRO)/tools/bin/uam

# No thread_local anywhere (the module loader sets up no TLS block), no exceptions,
# no RTTI, no thread-safe statics (their guards would need the loader's libstdc++).
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC
FLAGS    := -g -O2 -Wall -ffunction-sections -fdata-sections $(ARCH) -D__SWITCH__ \
            -I$(LIBNX)/include -I$(NEKO)/include -I$(BUILD)
CXXFLAGS := $(FLAGS) -std=gnu++20 -fno-rtti -fno-exceptions -fno-threadsafe-statics

SHADERS := fullscreen_vsh haze_fsh glass_fsh glassfront_fsh ui_vsh ui_fsh mesh_vsh mesh_fsh icon_fsh cubequad_vsh box_fsh verre_fsh glassbg_fsh glasscube_fsh cubebox_vsh uilite_fsh uisurf_fsh
DKSH    := $(patsubst %,$(BUILD)/sh/%.dksh,$(SHADERS))
OBJS    := $(BUILD)/Entry.o $(BUILD)/Gpu.o $(BUILD)/Ui.o $(BUILD)/Lock.o $(BUILD)/Audio.o $(BUILD)/Assets.o $(BUILD)/Wheel.o $(BUILD)/Games.o $(BUILD)/System.o $(BUILD)/Settings.o $(BUILD)/Haptics.o $(BUILD)/Shelf.o $(BUILD)/Jpeg.o $(BUILD)/Album.o $(BUILD)/Apps.o $(BUILD)/Pads.o $(BUILD)/Profiles.o $(BUILD)/Nextendo.o $(BUILD)/Covers.o $(BUILD)/Notify.o $(BUILD)/Popup.o $(BUILD)/Ntp.o $(BUILD)/Lang.o $(BUILD)/Theme.o $(BUILD)/Clock.o $(BUILD)/DatePages.o $(BUILD)/stub.o $(BUILD)/crt0.o

all: $(BUILD)/brume.dnro

# v82 - Réglages > Son > Audio Bluetooth, Brume's own page (btm:sys); stub.o and crt0.o stay last
OBJS := $(filter-out $(BUILD)/stub.o $(BUILD)/crt0.o,$(OBJS)) $(BUILD)/BtAudio.o $(BUILD)/stub.o $(BUILD)/crt0.o
$(BUILD)/BtAudio.o: src/BtAudio.cpp src/BtAudio.hpp src/Notify.hpp src/Popup.hpp src/Audio.hpp src/Ease.hpp src/Haptics.hpp src/Lang.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@

# v85 - Réglages > Système > Journal de Brume (the latest errors of perf.log and of the overlay's journal); stub.o and crt0.o stay last
OBJS := $(filter-out $(BUILD)/stub.o $(BUILD)/crt0.o,$(OBJS)) $(BUILD)/Journal.o $(BUILD)/stub.o $(BUILD)/crt0.o
$(BUILD)/Journal.o: src/Journal.cpp src/Journal.hpp src/Audio.hpp src/Ease.hpp src/Haptics.hpp src/Lang.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@

# v87 - the details pass: the orbiting lights (Busy), the charger (Charge), the lock code like the
# PS4's (Pin); stub.o and crt0.o stay last
OBJS := $(filter-out $(BUILD)/stub.o $(BUILD)/crt0.o,$(OBJS)) $(BUILD)/Busy.o $(BUILD)/Charge.o $(BUILD)/Pin.o $(BUILD)/stub.o $(BUILD)/crt0.o
$(BUILD)/Busy.o $(BUILD)/Charge.o $(BUILD)/Pin.o: $(BUILD)/%.o: src/%.cpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@

# v90 - the emulators found by themselves (Emus: the wheel shows the consoles that have one); stub.o and crt0.o stay last
OBJS := $(filter-out $(BUILD)/stub.o $(BUILD)/crt0.o,$(OBJS)) $(BUILD)/Emus.o $(BUILD)/stub.o $(BUILD)/crt0.o
$(BUILD)/Emus.o: src/Emus.cpp src/Emus.hpp src/Notify.hpp src/Lang.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@

# v95 - the Amis page (Nextendo) and the Discord voice QR (Social), the QR codes (Qr: Nayuki's generator, MIT, compiled
# inside Qr.cpp); stub.o and crt0.o stay last
OBJS := $(filter-out $(BUILD)/stub.o $(BUILD)/crt0.o,$(OBJS)) $(BUILD)/Social.o $(BUILD)/Qr.o $(BUILD)/stub.o $(BUILD)/crt0.o
$(BUILD)/Social.o: src/Social.cpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Qr.o: src/Qr.cpp src/Qr.hpp src/third_party/qrcodegen.c src/third_party/qrcodegen.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@

# v98 - Spotify (the Brume Musique service's panel and top bar line, docs/SPOTIFY.md); stub.o and crt0.o stay last
OBJS := $(filter-out $(BUILD)/stub.o $(BUILD)/crt0.o,$(OBJS)) $(BUILD)/Spotify.o $(BUILD)/stub.o $(BUILD)/crt0.o
$(BUILD)/Spotify.o: src/Spotify.cpp src/Spotify.hpp musique/switify/phase2/common/include/switify/ipc_protocol.h $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@

# v96 - the profile page (MyPage: who, the friend code, the play time from pdm:qry, the profile's choices); stub.o and crt0.o stay last
OBJS := $(filter-out $(BUILD)/stub.o $(BUILD)/crt0.o,$(OBJS)) $(BUILD)/MyPage.o $(BUILD)/stub.o $(BUILD)/crt0.o
$(BUILD)/MyPage.o: src/MyPage.cpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@

# v56 (review): every object depends on every header - a class that changes rebuilds all (no stale objects)
$(filter-out $(BUILD)/stub.o $(BUILD)/crt0.o,$(OBJS)): $(wildcard src/*.hpp)

$(BUILD)/brume.dnro: $(BUILD)/brume.elf
	python3 $(DLINK)/tools/elf2dnro.py $< $@

# -z nopack-relative-relocs: the module loader applies RELA, not RELR.
$(BUILD)/brume.elf: $(OBJS)
	$(A64)g++ -specs=$(DLINK)/tools/module.specs -g $(ARCH) -shared -nodefaultlibs \
	    -Wl,--export-dynamic -Wl,-Bsymbolic -Wl,--gc-sections -Wl,-z,nopack-relative-relocs \
	    -Wl,-Map,$(BUILD)/brume.map -o $@ $(OBJS) -lgcc
	$(A64)nm -D -u $@ | awk '{print $$NF}' | sort -u > $(BUILD)/brume.undef

$(BUILD)/Entry.o: src/Entry.cpp src/Clock.hpp src/DatePages.hpp src/Pads.hpp src/Apps.hpp src/Album.hpp src/Ease.hpp src/Shelf.hpp src/Mesh3d.hpp src/Gpu.hpp src/Ui.hpp src/Lock.hpp src/Audio.hpp src/Assets.hpp src/Wheel.hpp src/Games.hpp src/System.hpp src/Settings.hpp src/Haptics.hpp $(BUILD)/shaders.h $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Gpu.o: src/Gpu.cpp src/Gpu.hpp
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/stub.o: $(QEXT_DIR)/menu/stub/stub.c
	@mkdir -p $(BUILD)
	$(A64)gcc -g $(ARCH) -c $< -o $@
$(BUILD)/crt0.o: $(QEXT_DIR)/menu/stub/crt0.s
	@mkdir -p $(BUILD)
	$(A64)gcc -g $(ARCH) -c $< -o $@

$(BUILD)/Ui.o: src/Ui.cpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Lock.o: src/Lock.cpp src/Clock.hpp src/Lock.hpp src/Audio.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Audio.o: src/Audio.cpp src/Audio.hpp
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Assets.o: src/Assets.cpp src/Assets.hpp src/Gpu.hpp $(BUILD)/shaders.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/System.o: src/System.cpp src/System.hpp src/Ease.hpp src/Settings.hpp src/Assets.hpp src/Audio.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Games.o: src/Games.cpp src/Games.hpp src/Ease.hpp src/Shelf.hpp src/Mesh3d.hpp src/Wheel.hpp src/Assets.hpp src/Audio.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Wheel.o: src/Wheel.cpp src/Settings.hpp src/Wheel.hpp src/Ease.hpp src/Mesh3d.hpp src/Assets.hpp src/Audio.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/ui_layout.h $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
# The wheel's layout, measured by the browser on the mockup's CSS (tools/bake-assets.html).
$(BUILD)/ui_layout.h: assets/ui/layout.json tools/ui2h.py
	@mkdir -p $(BUILD)
	python3 tools/ui2h.py $@ $<

# Text: glyph atlases pre-rendered on the PC (host FreeType), see tools/fontbake.cpp.
$(BUILD)/fontbake: tools/fontbake.cpp
	@mkdir -p $(BUILD)
	g++ -O2 -std=c++17 $< -o $@ $$(pkg-config --cflags --libs freetype2)
$(BUILD)/fonts.h: $(BUILD)/fontbake $(wildcard assets/fonts/*.ttf)
	@mkdir -p $(BUILD)/polices
	$(BUILD)/fontbake $@ assets/fonts $(BUILD)/polices

# Shaders: GLSL 460 -> deko3d binaries (uam). Fragment shaders get common.glsl
# (the shared uniform block) after the #version line.
$(BUILD)/sh/%_vsh.dksh: shaders/%_vsh.glsl
	@mkdir -p $(BUILD)/sh
	$(UAM) -s vert -o $@ $<
$(BUILD)/sh/%_fsh.dksh: shaders/%_fsh.glsl shaders/common.glsl
	@mkdir -p $(BUILD)/sh
	{ echo '#version 460'; cat shaders/common.glsl $<; } > $(BUILD)/sh/$*_fsh.glsl
	$(UAM) -s frag -o $@ $(BUILD)/sh/$*_fsh.glsl
$(BUILD)/shaders.h: $(DKSH) tools/bin2h.py
	python3 tools/bin2h.py $@ $(DKSH)

clean:
	rm -rf $(BUILD)
.PHONY: all clean
# v28 - the settings page (carousel of glass icons)
$(BUILD)/Settings.o: src/Settings.cpp src/Clock.hpp src/DatePages.hpp src/Settings.hpp src/Haptics.hpp src/Wheel.hpp src/Assets.hpp src/Audio.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
# v31 - the game boxes: model + covers (Shelf), the Switch icons' JPEG (stb_image, no TLS)
$(BUILD)/Shelf.o: src/Shelf.cpp src/Shelf.hpp src/Gpu.hpp
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Album.o: src/Album.cpp src/Album.hpp src/Shelf.hpp src/Wheel.hpp src/Mesh3d.hpp src/Ease.hpp src/Gpu.hpp src/Ui.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Apps.o: src/Apps.cpp src/Apps.hpp src/Games.hpp src/Shelf.hpp src/Wheel.hpp src/Mesh3d.hpp src/Ease.hpp src/Gpu.hpp src/Ui.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Pads.o: src/Pads.cpp src/Pads.hpp src/Mesh3d.hpp src/Assets.hpp src/Gpu.hpp src/Wheel.hpp src/Settings.hpp src/Ease.hpp src/Ui.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Profiles.o: src/Profiles.cpp src/Profiles.hpp src/Shelf.hpp src/Settings.hpp src/Ease.hpp src/Gpu.hpp src/Ui.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Lang.o: src/Lang.cpp src/Lang.hpp src/LangTable.inc src/Ui.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Theme.o: src/Theme.cpp src/Theme.hpp src/Settings.hpp src/Ui.hpp src/Notify.hpp src/Audio.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Nextendo.o: src/Nextendo.cpp src/Nextendo.hpp src/Settings.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/Ntp.o: src/Ntp.cpp src/Clock.hpp src/Ntp.hpp src/Nextendo.hpp src/Settings.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
# v77 - the notifications and the task centre
$(BUILD)/Notify.o: src/Notify.cpp src/Notify.hpp src/Audio.hpp src/Ease.hpp src/Haptics.hpp src/Lang.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
# v78 - the popups
$(BUILD)/Popup.o: src/Popup.cpp src/Popup.hpp src/Notify.hpp src/Audio.hpp src/Ease.hpp src/Haptics.hpp src/Lang.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
# v75 - the covers downloaded by the console: PNG through zlib (the loader exports zlib; its header from devkitPro's portlibs)
$(BUILD)/Covers.o: src/Covers.cpp src/Covers.hpp src/Nextendo.hpp src/Shelf.hpp src/Settings.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -I$(DEVKITPRO)/portlibs/switch/include -c $< -o $@
$(BUILD)/Jpeg.o: src/Jpeg.cpp src/third_party/stb_image.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
# v29 - HD rumble as soft haptic clicks
$(BUILD)/Haptics.o: src/Haptics.cpp src/Haptics.hpp
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
# v29.2 - the water cubes' second pass: glass_fsh built with FRONT (pass 1 stays lean)
$(BUILD)/sh/glassfront_fsh.dksh: shaders/glass_fsh.glsl shaders/common.glsl
	@mkdir -p $(BUILD)/sh
	{ echo '#version 460'; echo '#define FRONT 1'; cat shaders/common.glsl shaders/glass_fsh.glsl; } > $(BUILD)/sh/glassfront_fsh.glsl
	$(UAM) -s frag -o $@ $(BUILD)/sh/glassfront_fsh.glsl
# v90 (perf) - the full-screen glass pass in two: the room alone over the whole screen (glassbg), then the water cubes only in a
# square around each one (cubebox_vsh + glasscube): the same pixels, ~2-3x fewer instructions per pixel of the screen
$(BUILD)/sh/glassbg_fsh.dksh: shaders/glass_fsh.glsl shaders/common.glsl
	@mkdir -p $(BUILD)/sh
	{ echo '#version 460'; echo '#define BG 1'; cat shaders/common.glsl shaders/glass_fsh.glsl; } > $(BUILD)/sh/glassbg_fsh.glsl
	$(UAM) -s frag -o $@ $(BUILD)/sh/glassbg_fsh.glsl
$(BUILD)/sh/glasscube_fsh.dksh: shaders/glass_fsh.glsl shaders/common.glsl
	@mkdir -p $(BUILD)/sh
	{ echo '#version 460'; echo '#define CUBES 1'; cat shaders/common.glsl shaders/glass_fsh.glsl; } > $(BUILD)/sh/glasscube_fsh.glsl
	$(UAM) -s frag -o $@ $(BUILD)/sh/glasscube_fsh.glsl
$(BUILD)/sh/cubebox_vsh.dksh: shaders/cubebox_vsh.glsl shaders/common.glsl
	@mkdir -p $(BUILD)/sh
	{ echo '#version 460'; cat shaders/common.glsl shaders/cubebox_vsh.glsl; } > $(BUILD)/sh/cubebox_vsh.glsl
	$(UAM) -s vert -o $@ $(BUILD)/sh/cubebox_vsh.glsl
# v83 - the console's clock written by the menu (Clock), Réglages > Système > Date et heure's own pages (DatePages)
$(BUILD)/Clock.o: src/Clock.cpp src/Clock.hpp
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
$(BUILD)/DatePages.o: src/DatePages.cpp src/DatePages.hpp src/Clock.hpp src/Audio.hpp src/Ease.hpp src/Haptics.hpp src/Lang.hpp src/Notify.hpp src/Popup.hpp src/Settings.hpp src/Ui.hpp src/Gpu.hpp $(BUILD)/fonts.h
	@mkdir -p $(BUILD)
	$(A64)g++ $(CXXFLAGS) -c $< -o $@
