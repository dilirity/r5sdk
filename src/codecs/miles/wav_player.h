#pragma once

#include "tier1/utlstring.h"

namespace WavPlayer
{
	// Initialize SDL audio device lazily on first call.
	bool PlayOneShot(const char* wavPath);
}


