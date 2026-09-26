#pragma once

#include <sdk/includes.h>

namespace flaway
{
	namespace modules
	{
		namespace backtrack_hook
		{
			bool init();
			void shutdown();
			void set_delay_enabled(bool enabled);
			void set_delay_ms(int delay_ms);
		}
	}
}
