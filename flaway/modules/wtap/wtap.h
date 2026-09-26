#pragma once

#include <jni.h>

namespace flaway
{
	namespace modules
	{
		namespace wtap
		{
			void run();
			void on_hit(); // Called when a hit is registered
		}
	}
}
