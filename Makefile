# Linux build for flaway.so
# Requires: g++, JDK, X11, GL, Xtst development packages

CXX = g++
JAVA_HOME ?= $(or $(word 1,$(wildcard /usr/lib/jvm/java-21-openjdk-amd64 /usr/lib/jvm/java-17-openjdk-amd64 /usr/lib/jvm/default-java)),/usr/lib/jvm/java-17-openjdk-amd64)
JAVA_INC = $(JAVA_HOME)/include
# Vendored Vulkan headers keep the build working without libvulkan-dev
VULKAN_INC = third_party/vulkan_headers
CXXFLAGS = -std=c++20 -Wall -O2 -fPIC -fvisibility=hidden -g -fno-omit-frame-pointer \
           -I. -Iutils -Iutils/imgui -Iutils/jnihook-master/include \
           -Isdk -I$(JAVA_INC) -I$(JAVA_INC)/linux -I$(VULKAN_INC) \
           -DNDEBUG -D_FLAWAY_EXPORTS

# Link against concrete sonames so the build works without -dev symlinks,
# and never against a newer glibc than the running system.
LDLIBS = -ldl -l:libX11.so.6 -l:libXtst.so.6

# Source files (shared between Windows and Linux)
SRCS = \
    flaway/flaway.cpp \
    flaway/globals/globals.cpp \
    flaway/config/config.cpp \
    flaway/hooks/Hook.cpp \
    flaway/gui/GUI.cpp \
    flaway/gui/glass_blur.cpp \
    flaway/utils/logger.cpp \
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

BUILD_DIR = build
TARGET = $(BUILD_DIR)/flaway.so

.PHONY: all clean injector

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(ALL_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ -shared $(LDLIBS)
	@echo "[+] Built $(TARGET)"
	@ls -lh $(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

injector:
	$(MAKE) -C injector/linux

clean:
	rm -f $(ALL_OBJS) $(TARGET)
	$(MAKE) -C injector/linux clean

# Check for required packages
check-deps:
	@echo "Checking dependencies..."
	@pkg-config --exists x11 || echo "WARNING: libX11-dev not found"
	@pkg-config --exists xtst || echo "WARNING: libXtst-dev not found"  
	@pkg-config --exists gl  || echo "WARNING: libgl-dev not found"
	@test -n "$(JAVA_HOME)" || echo "WARNING: JAVA_HOME not set"
	@echo "Done."

.PHONY: all clean injector check-deps
