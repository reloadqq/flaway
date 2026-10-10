#include "better_minecraft.h"
#include "bm_internal.h"
#include "../../flaway.h"
#include "../../hooks/Hook.h"
#include "../../globals/globals.h"

namespace flaway
{
	namespace modules
	{
		namespace better_minecraft
		{
			void run()
			{
				if (Hook::get_unhooked()) return;
				if (!flaway::instance || !flaway::instance->initialized) return;

				detail::init_hooks();
				detail::zoom_tick();
			}

			void draw()
			{
			}

			void shutdown()
			{
				globals::better_minecraft_enabled = false;
				detail::zoom_reset();
			}
		}
	}
}
