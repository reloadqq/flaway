# Linux build for flaway.so
# Requires: g++, JDK, X11, GL, Xtst development packages

CXX = g++
JAVA_HOME ?= $(or $(word 1,$(wildcard /usr/lib/jvm/java-21-openjdk-amd64 /usr/lib/jvm/java-17-openjdk-amd64 /usr/lib/jvm/default-java)),/usr/lib/jvm/java-17-openjdk-amd64)
JAVA_INC = $(JAVA_HOME)/include
# Vendored Vulkan headers keep the build working without libvulkan-dev
VULKAN_INC = third_party/vulkan_headers
# -g1 (line tables only): crash_dump prints library+offset so the .so stays
# addr2line-able, but full -g makes GUI.cpp peak at ~580 MB and OOMs the box.
DEBUGFLAGS = $(if $(filter 1,$(DEBUG)),-g,-g1)
CXXFLAGS = -std=c++20 -Wall -O2 -fPIC -fvisibility=hidden $(DEBUGFLAGS) -fno-omit-frame-pointer \
           -include flaway/utils/no_log.h \
           -I. -Iutils -Iutils/imgui -Iutils/jnihook-master/include \
           -Isdk -I$(JAVA_INC) -I$(JAVA_INC)/linux -I$(VULKAN_INC) \
           -DNDEBUG -D_FLAWAY_EXPORTS
# Header dependency tracking: editing a header rebuilds the .o's that include it
DEPFLAGS = -MMD -MP

# Bounded parallelism: bare `make -j` means UNLIMITED jobs in GNU make, i.e.
# one compiler per source file, and GUI.cpp alone needs ~500 MB. The default
# `all` target below re-runs the real build with at most JOBS compilers.
JOBS ?= 2

# Link against concrete sonames so the build works without -dev symlinks,
# and never against a newer glibc than the running system.
LDLIBS = -ldl -l:libX11.so.6 -l:libXtst.so.6 -lz

# Source files (shared between Windows and Linux)
SRCS = \
    flaway/flaway.cpp \
    flaway/globals/globals.cpp \
    flaway/config/config.cpp \
    flaway/hooks/Hook.cpp \
    flaway/gui/GUI.cpp \
    flaway/gui/vk_context.cpp \
    flaway/gui/glass_blur.cpp \
    flaway/gui/hud/hud_core.cpp \
    flaway/gui/hud/hud_data.cpp \
    flaway/gui/hud/hud_elements.cpp \
    flaway/gui/hud/hud_icons.cpp \
    flaway/gui/hud/hud_fx.cpp \
    flaway/utils/logger.cpp \
    flaway/utils/ah_utils.cpp \
    flaway/utils/rlog.cpp \
    flaway/utils/chat_notify.cpp \
    flaway/utils/discord_rpc.cpp \
    flaway/modules/mace/mace.cpp \
    flaway/modules/shield_breaker/shield_breaker.cpp \
    flaway/modules/hitbox/hitbox.cpp \
    flaway/modules/aimassist/aimassist.cpp \
    flaway/modules/triggerbot/triggerbot.cpp \
    flaway/modules/pearl_catch/pearl_catch.cpp \
    flaway/modules/reach/reach.cpp \
    flaway/modules/reach/reach_hook.cpp \
    flaway/modules/esp/esp.cpp \
    flaway/modules/stun_slam/stun_slam.cpp \
    flaway/modules/server_rotation/server_rotation.cpp \
    flaway/modules/stap/stap.cpp \
    flaway/modules/wtap/wtap.cpp \
    flaway/modules/anchor_macro/anchor_macro.cpp \
    flaway/modules/storage_esp/storage_esp.cpp \
    flaway/modules/base_finder/base_finder.cpp \
    flaway/modules/autocrystal/autocrystal.cpp \
    flaway/modules/autototem/autototem.cpp \
    flaway/modules/autojumpreset/autojumpreset.cpp \
    flaway/modules/backtrack/backtrack.cpp \
    flaway/modules/backtrack/backtrack_hook.cpp \
    flaway/modules/nametag_hook/nametag_hook.cpp \
    flaway/modules/gambling/gambling.cpp \
    flaway/modules/modules.cpp \
    flaway/modules/chest_stealer/chest_stealer.cpp \
    flaway/modules/autosprint/autosprint.cpp \
    flaway/modules/fullbright/fullbright.cpp \
    flaway/modules/fog/fog.cpp \
    flaway/modules/friend_manager/friend_manager.cpp \
    flaway/modules/chat_command/chat_command.cpp \
    flaway/utils/crash_dump.cpp

# Linux platform sources
LINUX_SRCS = \
    platform/linux/entry.cpp \
    platform/linux/x11_helper.cpp \
    platform/linux/inline_hook.cpp

# SDK sources
SDK_SRCS = \
    sdk/classloader.cpp \
    sdk/minecraft/minecraft.cpp \
    sdk/minecraft/player/player.cpp \
    sdk/minecraft/world/world.cpp \
    sdk/minecraft/entity/entity.cpp \
    sdk/minecraft/util/box.cpp

# ImGui sources
IMGUI_SRCS = \
    utils/imgui/imgui.cpp \
    utils/imgui/imgui_draw.cpp \
    utils/imgui/imgui_widgets.cpp \
    utils/imgui/imgui_tables.cpp \
    utils/imgui/imgui_impl_opengl3.cpp

# JNIHook sources
JNIHOOK_SRCS = \
    utils/jnihook-master/src/jnihook.cpp \
    utils/jnihook-master/src/classfile.cpp \
    utils/jnihook-master/src/uuid.cpp

ALL_SRCS = $(SRCS) $(LINUX_SRCS) $(SDK_SRCS) $(IMGUI_SRCS) $(JNIHOOK_SRCS)
ALL_OBJS = $(ALL_SRCS:.cpp=.o)
ALL_DEPS = $(ALL_OBJS:.o=.d)

BUILD_DIR = build
TARGET = $(BUILD_DIR)/flaway.so

.PHONY: all build clean check-deps

# `all` only bounds the job count; the real build lives in `build`.
# NOTE: the phony target is called `build` while the directory is also `build`;
# never list $(BUILD_DIR) as a prerequisite of the `build` target - make treats
# it as a self-dependency, drops the edge and the mkdir recipe ends up running
# AFTER the link. The directory is created via the order-only prerequisite below.
all:
	+@$(MAKE) --no-print-directory -j$(JOBS) build

build: $(TARGET)

# Create the output dir inline instead of via a separate `build` target: the
# phony target name and the directory name are the same, which made make infer
# "build depends on build/flaway.so" (a cycle) and print
#   "Cyclic dependency build/flaway.so <- build dropped".
$(TARGET): $(ALL_OBJS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ $^ -shared $(LDLIBS)
	@echo "[+] Built $(TARGET)"
	@ls -lh $(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

clean:
	rm -f $(ALL_OBJS) $(ALL_DEPS) $(TARGET)

-include $(ALL_DEPS)

# Check for required packages
check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists x11 || echo "WARNING: libX11-dev not found"
	@pkg-config --exists xtst || echo "WARNING: libXtst-dev not found"  
	@pkg-config --exists gl  || echo "WARNING: libgl-dev not found"
	@test -n "$(JAVA_HOME)" || echo "WARNING: JAVA_HOME not set"
	@echo "Done."
