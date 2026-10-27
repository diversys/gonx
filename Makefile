# Gonx look for Renku OS (and Haiku).
#
# The decorator is an app_server add-on: it is compiled against app_server's
# private headers and linked against the app_server binary of the system it
# will run on, so it should be built on (or for) that system.
#
# On Renku or Haiku itself, `make` builds against the running system, using
# the private headers bundled in build-headers/ (from a Renku source tree;
# FreeType's too). install.sh does this for you.
#
# Cross-build on Linux, against a Renku source tree and a system's sysroot:
#
#   sh tools/sync-sysroot.sh <ssh-host>     # once per Renku build
#   make

RENKU_SRC ?= $(HOME)/Projects/renku-src
SYSROOT ?= $(CURDIR)/build/sysroot

ifeq ($(shell uname -s),Haiku)
CXX ?= g++
APP_SERVER ?= /boot/system/servers/app_server
RC ?= rc
XRES ?= xres
SYSROOT_FLAGS :=
else
CROSS ?= $(RENKU_SRC)/generated.x86_64/cross-tools-x86_64/bin/x86_64-unknown-haiku-
CXX := $(CROSS)g++
APP_SERVER ?= $(SYSROOT)/boot/system/servers/app_server
RC ?= ssh $(RC_HOST) rc
XRES ?= $(RENKU_SRC)/generated.x86_64/objects/linux/x86_64/release/tools/xres
SYSROOT_FLAGS := --sysroot=$(SYSROOT)
endif

ifeq ($(shell uname -s),Haiku)
HEADER_ROOT ?= $(CURDIR)/build-headers/renku
FREETYPE_HEADERS ?= $(CURDIR)/build-headers/freetype2
else
HEADER_ROOT ?= $(RENKU_SRC)
FREETYPE_HEADERS ?= $(SYSROOT)/boot/system/develop/headers/freetype2
endif

APP := $(HEADER_ROOT)/src/servers/app
PRIVATE := $(HEADER_ROOT)/headers/private

DECOR_INCLUDES := \
	-iquote $(APP) -iquote $(APP)/stackandtile -iquote $(APP)/decorator \
	-iquote $(APP)/drawing -iquote $(APP)/drawing/Painter -iquote $(APP)/font \
	-I$(PRIVATE) -I$(PRIVATE)/app -I$(PRIVATE)/shared \
	-I$(PRIVATE)/interface -I$(PRIVATE)/graphics \
	-I$(HEADER_ROOT)/headers/libs/agg \
	-I$(FREETYPE_HEADERS)

override CXXFLAGS += -O2 -Wall -Wno-multichar -fPIC $(SYSROOT_FLAGS)

BUILD := build
DECOR := $(BUILD)/Gonx
THEME := $(BUILD)/gonx-theme
LOOK := $(BUILD)/control_look/Gonx

all: $(DECOR) $(LOOK) $(THEME)

$(THEME): src/theme/GonxTheme.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(PRIVATE)/interface -s -o $@ $< -lbe

$(BUILD)/GonxDecorator.o: src/decorator/GonxDecorator.cpp src/decorator/GonxDecorator.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(DECOR_INCLUDES) -c $< -o $@

# Resources are compiled by Haiku's rc; when cross-building, RC_HOST names a
# Haiku/Renku machine reachable over ssh (rc reads stdin, writes the file).
RC_HOST ?= renku-test
$(BUILD)/%.rsrc:
	@mkdir -p $(BUILD)
ifeq ($(shell uname -s),Haiku)
	$(RC) -o $@ $<
else
	ssh $(RC_HOST) 'cat > /tmp/gonx.rdef && rc -o /tmp/gonx.rsrc /tmp/gonx.rdef && cat /tmp/gonx.rsrc' < $< > $@
endif

$(BUILD)/GonxDecorator.rsrc: src/decorator/resources.rdef
$(BUILD)/GonxControlLook.rsrc: src/controllook/GonxControlLook.rdef

# Strip at link time: xres appends after the ELF data.
$(DECOR): $(BUILD)/GonxDecorator.o $(BUILD)/GonxDecorator.rsrc
	$(CXX) $(SYSROOT_FLAGS) -shared -s -o $@ $(BUILD)/GonxDecorator.o \
		-lbe $(APP_SERVER) -lstdc++
	$(XRES) -o $@ $(BUILD)/GonxDecorator.rsrc

# The control look runs inside every application, against libbe only.
$(BUILD)/GonxControlLook.o: src/controllook/GonxControlLook.cpp src/controllook/GonxControlLook.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(PRIVATE)/interface -c $< -o $@

$(LOOK): $(BUILD)/GonxControlLook.o $(BUILD)/GonxControlLook.rsrc
	@mkdir -p $(dir $@)
	$(CXX) $(SYSROOT_FLAGS) -shared -s -o $@ $(BUILD)/GonxControlLook.o \
		-lbe -lstdc++
	$(XRES) -o $@ $(BUILD)/GonxControlLook.rsrc

# A window with every kind of control, for checking the control look.
demo: $(BUILD)/controls-demo
$(BUILD)/controls-demo: tools/controls-demo.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -s -o $@ $< -lbe -lstdc++

clean:
	rm -f $(BUILD)/*.o $(BUILD)/*.rsrc $(DECOR) $(LOOK) $(THEME)

.PHONY: all clean demo

# The package is put together by Haiku's own "package" tool; when
# cross-building that runs on RC_HOST.
hpkg: all
ifeq ($(shell uname -s),Haiku)
	sh packaging/build-hpkg.sh $(BUILD)
else
	tar cf - $(DECOR) $(LOOK) $(THEME) packaging README.md LICENSE \
		| ssh $(RC_HOST) 'rm -rf /tmp/gonx-pkg && mkdir /tmp/gonx-pkg && cd /tmp/gonx-pkg && tar xmf - && sh packaging/build-hpkg.sh build'
	scp '$(RC_HOST):/tmp/gonx-pkg/gonx-*.hpkg' $(BUILD)/
endif

.PHONY: hpkg
