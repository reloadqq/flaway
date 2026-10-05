#pragma once

#include <sdk/includes.h>
#include <string>

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

// A block the target scanner recognised (chest, shulker, spawner, portal,
// obsidian). `type` uses the target_type enum of base_finder.cpp.
struct base_finder_target
{
	int x, y, z;
	int type;
};

// A live player the target scanner tracks (tracers + chat/file report).
struct base_finder_player
{
	int id;
	double x, y, z;
	std::string name;
};
