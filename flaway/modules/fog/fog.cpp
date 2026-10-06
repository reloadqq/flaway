#include "fog.h"
#include "../../globals/globals.h"
#include "../../gui/GUI.h"
#include "../../../utils/imgui/imgui.h"

namespace flaway
{
	namespace modules
	{
		namespace fog
		{
			// Screen-space fog overlay. No JNI/FogRenderer hooks: a background
			// draw-list gradient (edge vignette / full tint / bottom fog) that
			// sits under ESP boxes and HUD.
			void run()
			{
				// Nothing to poll: settings live in globals, draw() reads them.
			}

			void draw()
			{
				if (!globals::fog_enabled) return;
				if (!GUI::get_is_init()) return;
				ImGuiIO& io = ImGui::GetIO();
				float sw = io.DisplaySize.x, sh = io.DisplaySize.y;
				if (sw <= 1.0f || sh <= 1.0f) return;
				ImDrawList* dl = ImGui::GetBackgroundDrawList();
				if (!dl) return;
				float k = globals::fog_strength / 100.0f;
				if (k <= 0.001f) return;
				int r = (int)(globals::fog_color[0] * 255.0f);
				int g = (int)(globals::fog_color[1] * 255.0f);
				int b = (int)(globals::fog_color[2] * 255.0f);
				int a0 = (int)(globals::fog_color[3] * 255.0f * k);
				if (a0 <= 0) return;
				if (a0 > 255) a0 = 255;
				const ImU32 c_edge = IM_COL32(r, g, b, a0);
				const ImU32 c_clear = IM_COL32(r, g, b, 0);
				if (globals::fog_mode == 1) {
					// Full-screen tint
					dl->AddRectFilled(ImVec2(0, 0), ImVec2(sw, sh), c_edge);
					return;
				}
				if (globals::fog_mode == 2) {
					// Bottom fog (ground haze)
					float ey = sh * 0.35f;
					dl->AddRectFilledMultiColor(ImVec2(0, sh - ey), ImVec2(sw, sh),
						c_clear, c_clear, c_edge, c_edge);
					return;
				}
				// Mode 0: edge vignette
				float ex = sw * 0.22f, ey = sh * 0.22f;
				dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ex, sh),
					c_edge, c_clear, c_edge, c_clear);
				dl->AddRectFilledMultiColor(ImVec2(sw - ex, 0), ImVec2(sw, sh),
					c_clear, c_edge, c_clear, c_edge);
				dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(sw, ey),
					c_edge, c_edge, c_clear, c_clear);
				dl->AddRectFilledMultiColor(ImVec2(0, sh - ey), ImVec2(sw, sh),
					c_clear, c_clear, c_edge, c_edge);
			}

			void cleanup() {}
		}
	}
}
