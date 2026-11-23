# Brume Overlay: make -> brume-overlay.nso + brume-overlay.npdm (= exefs/main + exefs/main.npdm for 010000000000100C)
TARGET   := brume-overlay
BUILD    := build
DEFINES  :=
APP_JSON  = $(TOPDIR)/overlay.json
include $(dir $(abspath $(lastword $(MAKEFILE_LIST))))common.mk
ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)
CPPFILES        := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
export LD       := $(CXX)
export OFILES   := $(CPPFILES:.cpp=.o)
export INCLUDE  := $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
.PHONY: $(BUILD) clean all
all: $(BUILD)
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -fr $(BUILD) $(TARGET).nso $(TARGET).npdm $(TARGET).nsp $(TARGET).elf
else
.PHONY: all
DEPENDS := $(OFILES:.o=.d)
all: $(OUTPUT).nso $(OUTPUT).npdm
$(OUTPUT).nso: $(OUTPUT).elf
$(OUTPUT).npdm: $(APP_JSON)
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
