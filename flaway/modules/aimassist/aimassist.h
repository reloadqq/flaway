#pragma once

#include <sdk/includes.h>

namespace flaway
{
	namespace modules
	{
		class aimassist
		{
		public:
			static void run();
			static void draw_fov();
			static int get_locked_id();
			static void cleanup();
		};
	}
}
