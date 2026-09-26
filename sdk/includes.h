#pragma once

#include <vector>
#include <memory>
#include <jni.h>
#include <thread>
#include <iostream>
#include <mutex>
#include <cmath>
#include <cstdint>

#ifdef _WIN32
#include <Windows.h>
#include <gl/GL.h>
#include "../utils/minhook/include/MinHook.h"
#include "../utils/imgui/imgui_impl_win32.h"
#else
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#include "../platform/linux/inline_hook.h"
#include "../platform/linux/x11_helper.h"
#include "../platform/linux/windows_compat.h"
#endif

#include "../utils/imgui/imgui.h"
#include "../utils/imgui/imgui_impl_opengl3.h"
#include "mappings/mappings.hpp"
#include "../utils/jnihook-master/include/jnihook.h"
