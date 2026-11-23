# Brume Overlay - shared rules (Makefile = the overlay, preview.mk = the preview .nro). devkitPro's templates, trimmed.
# v90: -O3 (NEON: the copies and fades go 4 pixels at a time); no library but libnx (the text is baked, no FreeType)
ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO n'est pas defini")
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules
SOURCES  := source
INCLUDES :=
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS   := -g -Wall -O3 -ffunction-sections $(ARCH) $(DEFINES) $(INCLUDE) -D__SWITCH__
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS  := -g $(ARCH)
LDFLAGS   = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS     := -lnx -lm
LIBDIRS  := $(PORTLIBS) $(LIBNX)
