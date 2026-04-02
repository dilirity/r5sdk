#ifndef SCRIPTNETDATA_EXT_H
#define SCRIPTNETDATA_EXT_H

//------------------------------------------------------------------------------
// ScriptNetData Extension — SNDC_GLOBAL_NON_REWIND (category 5)
//
// Engine has 5 categories (0-4). We add GLOBAL_NON_REWIND as category 5.
// The engine's bounds check is
// binary-patched to allow category ≤5, and this module redirects all
// references to the scriptNetCategories array to an extended 6-entry copy.
//
// The category data struct (ScriptNetworkedCategoryData) is 0x20C (524) bytes:
//   +0x00: internalTypeMax[5]  (bool, range, bigint, time, entity)
//   +0x14: internalTypeCount[5]
//   +0x28: boolData[]
//   +0x38: rangeData[]  (uint16)
//   +0x78: bigintData[] (int32)
//   +0xA8: timeData[]   (float)
//   +0x108: varIndices[] (int32, registered var order for callbacks)
//------------------------------------------------------------------------------

#include "core/init.h"

// SDK-owned global pointer for the NonRewind entity
inline void* g_pScriptNetDataNonRewindEnt = nullptr;

// Var slot lookup for NonRewind natives — searches engine's scriptNetVars
// hash table for a variable name with the given category.
// Returns slot index, or -1 on failure.
int ScriptNetData_FindVarSlot(const char* name, int expectedCategory);

// Lifecycle — clear entity pointer on map shutdown
void ScriptNetDataExt_LevelShutdown();

class VScriptNetDataExt : public IDetour
{
	virtual void GetAdr(void) const {}
	virtual void GetFun(void) const;
	virtual void GetVar(void) const;
	virtual void GetCon(void) const {}
	virtual void Detour(const bool bAttach) const;
};

#endif // SCRIPTNETDATA_EXT_H
