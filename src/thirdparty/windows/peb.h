#ifndef WINDOWS_PEB_H
#define WINDOWS_PEB_H
#include "tebpeb64.h"

// taken from https://gist.github.com/Wack0/849348f9d4f3a73dac864a556e9372a5
// Gets a pointer to the PEB for x86, x64, ARM, ARM64, IA64, Alpha AXP, MIPS, and PowerPC.

// This relies on MS-compiler intrinsics.
// It has only been tested on x86/x64/ARMv7.

inline PEB* NtCurrentPeb() {
#ifdef _M_X64
	return (PEB*)(__readgsqword(0x60));
#elif _M_IX86
	return (PEB*)(__readfsdword(0x30));
#elif _M_ARM
	return *(PEB**)(_MoveFromCoprocessor(15, 0, 13, 0, 2) + 0x30);
#elif _M_ARM64
	return *(PEB**)(__getReg(18) + 0x60); // TEB in x18
#elif _M_IA64
	return *(PEB**)((size_t)_rdteb() + 0x60); // TEB in r13
#elif _M_ALPHA
	return *(PEB**)((size_t)_rdteb() + 0x30); // TEB pointer returned from callpal 0xAB
#elif _M_MIPS
	return *(PEB**)((*(size_t*)(0x7ffff030)) + 0x30); // TEB pointer located at 0x7ffff000 (PCR in user-mode) + 0x30
#elif _M_PPC
	// winnt.h of the period uses __builtin_get_gpr13() or __gregister_get(13) depending on _MSC_VER
	return *(PEB**)(__gregister_get(13) + 0x30); // TEB in r13
#else
#error "This architecture is currently unsupported"
#endif
}

#endif // WINDOWS_PEB_H
