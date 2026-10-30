# PocketFarm for New 3DS: native C port of ../PocketFarmWeb (see PORT.md).
#   make        → PocketFarm.3dsx
#   make pc     → build/pf, headless desktop harness (PPM shots in build/shots)
#   make oracle → build/oracle, the untouched web JS on QuickJS (reference output)
include $(DEVKITARM)/3ds_rules
TARGET := PocketFarm
WEB    := ../PocketFarmWeb
GAME   := $(filter-out src/plat_%.c src/cv_3ds.c,$(wildcard src/*.c)) vendor/cjson/cJSON.c
OBJS   := $(GAME:%.c=build/3ds/%.o) build/3ds/src/plat_3ds.o build/3ds/src/cv_3ds.o
ARCH   := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -O2 -g $(ARCH) -D__3DS__ -I$(DEVKITPRO)/libctru/include -Isrc -Ivendor -Ivendor/cjson -Wall -Wno-unused-function -Wno-missing-braces
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -L$(DEVKITPRO)/libctru/lib
LIBS   := -lcitro2d -lcitro3d -lctru -lm

all: $(TARGET).3dsx

# romfs = the web game's assets + data.json (every table its modules export)
romfs: FORCE
	@mkdir -p build/romfs
	@rsync -a --delete --exclude 'Cute_Fantasy/read_me.txt' $(WEB)/assets build/romfs/
	@node tools/dump.mjs $(WEB) build/romfs/data.json
	@arch -arm64 python3 tools/font.py build/romfs 2>/dev/null || python3 tools/font.py build/romfs   # make may run under Rosetta; PIL is arm64
	@$(MAKE) --no-print-directory build/pf && [ build/romfs/terrain/farm-spring.png -nt $(WEB)/assets/maps/farm.json ] || ./build/pf --bake build/romfs

$(TARGET).elf: $(OBJS) ; $(CC) $(LDFLAGS) $^ $(LIBS) -o $@
build/$(TARGET).smdh: meta/icon.png ; smdhtool --create "PocketFarm" "A cozy pocket farm: crops, animals and factories" "Jonatas Pimenta" $< $@
$(TARGET).3dsx: $(TARGET).elf build/$(TARGET).smdh romfs ; 3dsxtool $< $@ --smdh=build/$(TARGET).smdh --romfs=build/romfs
build/3ds/%.o: %.c src/*.h ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -c $< -o $@
clean: ; rm -rf build $(TARGET).elf $(TARGET).3dsx
FORCE:
.PHONY: all romfs clean pc oracle FORCE

build/pf: $(GAME) src/plat_pc.c src/*.h
	@mkdir -p build && cc -O2 -g -Wall -Wno-unused-function -Wno-missing-braces -Isrc -Ivendor -Ivendor/cjson $(GAME) src/plat_pc.c -lm -o $@
pc: romfs

# oracle: the QuickJS build that runs the web JS untouched
QJS := vendor/quickjs
QJS_SRCS := $(QJS)/quickjs.c $(QJS)/libregexp.c $(QJS)/libunicode.c $(QJS)/dtoa.c
build/oracle: oracle/source/host.c oracle/source/gfx.c oracle/pc/pc.c $(QJS_SRCS)
	@mkdir -p build && cc -O2 -g -w -I$(QJS) -Ivendor -Ioracle/source -D_GNU_SOURCE -DCONFIG_VERSION=\"pc\" $^ -lm -o $@
oracle: build/oracle
	@mkdir -p build/oracle-romfs && rsync -a $(WEB)/src $(WEB)/assets $(WEB)/index.html build/oracle-romfs/ && rsync -a oracle/romfs/ build/oracle-romfs/
