#pragma once

#include <jni.h>

namespace flaway
{
	namespace modules
	{
		namespace stap
		{
			void run();
			void on_hit(); // Called when a hit is registered
		}
	}
}
