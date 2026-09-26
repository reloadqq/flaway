#pragma once

#include <sdk/includes.h>

namespace flaway
{
	namespace modules
	{
		class base_finder
		{
		public:
			static void run();
			static void draw_boxes();
			static void shutdown();
		};
	}
}

struct base_finder_block
{
	int x, y, z;
};
