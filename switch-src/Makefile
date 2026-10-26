#---------------------------------------------------------------------------------
# PCTL All-in-One Rental Edition - Nintendo Switch NRO build file.
#
# Requires devkitA64 + libnx already installed. Use with:
#   export DEVKITPRO=/opt/devkitpro
#   export PATH=$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH
#   make -j$(nproc)
#
# You can inject your own rental salt / period at build time:
#   make RENTAL_SALT="my-secret-salt" RENTAL_PERIOD_DAYS=7
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

include $(DEVKITPRO)/libnx/switch_rules

TARGET    := pctltcp-allinone
BUILD     := build
OUTDIR    := out
SOURCES   := source

# Optional override via make args / env.
RENTAL_SALT        ?= CHANGE-MY-RENTAL-SALT-PLEASE-2025
RENTAL_PERIOD_DAYS ?= 7

APP_TITLE   := PCTL All-in-One Rental
APP_AUTHOR  := Rental Edition
APP_VERSION := 1.1.0

# ✅ 直接指定 libnx 所在目录，不再依赖 pkg-config
LIBDIRS     := $(DEVKITPRO)/libnx $(DEVKITPRO)/portlibs/switch

ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS      := -g -Wall -O2 -ffunction-sections -fdata-sections \
               -DDEFAULT_SALT=\"$(RENTAL_SALT)\" \
               -DDEFAULT_PERIOD_DAYS=$(RENTAL_PERIOD_DAYS) \
               -D__SWITCH__
CFLAGS      += $(ARCH) $(INCLUDE)
CXXFLAGS    := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS     := -g $(ARCH)
LDFLAGS     := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# ✅ 直接写死 libnx 的 lib 路径和库名，不再依赖 pkg-config
LIBS        := -L$(DEVKITPRO)/libnx/lib -lnx

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(OUTDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH  := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export OFILES_BIN :=
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN :=

export INCLUDE := $(foreach dir,$(SOURCES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@[ -d $(OUTDIR) ] || mkdir -p $(OUTDIR)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -rf $(BUILD) $(OUTDIR)

else

.PHONY: all

all : $(OUTPUT).nro

$(OUTPUT).nro : $(OUTPUT).elf
	@if [ -f $(TOPDIR)/icon.jpg ]; then \
		elf2nro $< $@ --icon=$(TOPDIR)/icon.jpg \
			--title="$(APP_TITLE)" --author="$(APP_AUTHOR)" --version="$(APP_VERSION)" ; \
	else \
		elf2nro $< $@ \
			--title="$(APP_TITLE)" --author="$(APP_AUTHOR)" --version="$(APP_VERSION)" ; \
	fi
	@echo " built ... $(notdir $@)"

$(OUTPUT).elf : $(OFILES)
	@echo linking $(notdir $@)
	@aarch64-none-elf-gcc $(LDFLAGS) $(OFILES) $(LIBS) -o $@
	@aarch64-none-elf-size $@

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h : %.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

endif
