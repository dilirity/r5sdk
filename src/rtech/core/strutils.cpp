//===========================================================================//
// 
// Purpose: string utilities
// 
//===========================================================================//
#include "strutils.h"

size_t R_UTF8_strncpy(char* const dest, const char* const source, const size_t maxBytes)
{
	const size_t length = strlen(source);
	const size_t copyCount = maxBytes - 1;

	if (length > maxBytes - 1)
	{
		size_t charIt = 0;
		const char* const bufIt = &source[length - copyCount];

		if ((*bufIt & 0xC0) == 0x80)
		{
			do
				charIt++;
			while ((bufIt[charIt] & 0xC0) == 0x80);
		}

		memcpy(dest, &bufIt[charIt], maxBytes - charIt);
		return copyCount - charIt;
	}

	memcpy(dest, source, length + 1); // +1 for null terminator.
	return length;
}
