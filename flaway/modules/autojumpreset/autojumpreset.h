#pragma once

#include <sdk/includes.h>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace flaway
{
	namespace modules
	{
		class autojumpreset
		{
		public:
			static void run();
			static void on_hit(); // Called when player takes damage
		};
	}
}

