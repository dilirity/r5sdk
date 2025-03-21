#include "core/stdafx.h"
#include "vguimatsurface/MatSystemSurface.h"

void MatSystemSurface_DrawColoredText(void* thisptr, short font, int fontHeight, int offsetX, int offsetY, int red, int green, int blue, int alpha, const char* fmt, ...)
{
	va_list argptr;
	va_start(argptr, fmt);
	CMatSystemSurface__DrawColoredTextInternal(thisptr, font, fontHeight, offsetX, offsetY, red, green, blue, alpha, 0, fmt, argptr);
	va_end(argptr);
}

