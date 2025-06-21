//===========================================================================//
//
// Purpose: A set of fast utilities for network addresses
//
//===========================================================================//
#ifndef TIER2_NETADRUTILS_H
#define TIER2_NETADRUTILS_H

#include "mathlib/ssemath.h"

struct IPv6Wrapper_s
{
	IPv6Wrapper_s(const in6_addr* rhs)
	{
		adr = *rhs;
	}

	IPv6Wrapper_s(const in6_addr& rhs)
	{
		adr = rhs;
	}

	IPv6Wrapper_s& operator=(const IPv6Wrapper_s& rhs)
	{
		adr = rhs.adr;
		return *this;
	}

	bool operator==(const IPv6Wrapper_s& rhs) const
	{
		const i64 UNALIGNED* const a = (i64 UNALIGNED*)this;
		const i64 UNALIGNED* const b = (i64 UNALIGNED*)&rhs;

		return (a[1] == b[1]) && (a[0] == b[0]);
	}

	in6_addr adr;
};

struct IPv6Hasher_s
{
	size_t operator()(const IPv6Wrapper_s& ip) const
	{
		const u32x4 data = LoadUnalignedSIMD(&ip.adr);
		const u32x4 xord = XorSIMD(data, RotateLeft(data));

		u64 hash = xord.m128_i64[0];

		hash ^= hash >> 33;
		hash *= 0xff51afd7ed558ccdULL;
		hash ^= hash >> 33;
		hash *= 0xc4ceb9fe1a85ec53ULL;
		hash ^= hash >> 33;

		return static_cast<size_t>(hash);
	}
};

#endif // TIER2_NETADRUTILS_H
