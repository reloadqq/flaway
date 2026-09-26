#pragma once

#include <sdk/includes.h>

namespace flaway
{
	namespace modules
	{
		namespace nametag_hook
		{
			bool init();
			void shutdown();
			void set_enabled(bool enabled);
			bool is_initialized();
		}
	}
}
