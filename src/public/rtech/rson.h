#pragma once
#include "mathlib/color.h"
#include "tier0/tslist.h"
#include "tier1/utlbuffer.h"
#include "filesystem/filesystem.h"

class RSON
{
public:
	enum eFieldType
	{
		RSON_NULL = 0x1,
		RSON_STRING = 0x2,
		RSON_VALUE = 0x4,
		RSON_OBJECT = 0x8,
		RSON_BOOLEAN = 0x10,
		RSON_INTEGER = 0x20,
		RSON_SIGNED_INTEGER = 0x40,
		RSON_UNSIGNED_INTEGER = 0x80,
		RSON_DOUBLE = 0x100,
		RSON_ARRAY = 0x1000,
	};


	//-------------------------------------------------------------------------
	// 
	//-------------------------------------------------------------------------
	struct Field_t;


	//-------------------------------------------------------------------------
	// 
	//-------------------------------------------------------------------------
	union Value_t
	{
		inline Field_t* GetSubKey() const { return subKey; };
		inline Value_t* GetSubValue() const { return subValue; };
		inline const char* GetString() const { return stringValue; };
		inline int64_t GetInt() const { return integerValue; };

		Field_t* subKey;
		Value_t* subValue;
		char* stringValue;
		int64_t integerValue;
	};

	//-------------------------------------------------------------------------
	// used for the root node of rson tree
	//-------------------------------------------------------------------------
	struct Node_t
	{
		eFieldType type;
		int valueCount;
		Value_t value;

		inline Field_t* GetFirstSubKey() const;
		inline Value_t* GetArrayValue(const int index) const;

		// does not support finding a key in a different level of the tree
		inline Field_t* FindKey(const char* const keyName) const;
	};

	//-------------------------------------------------------------------------
	// used for every other field of the rson tree
	//-------------------------------------------------------------------------
	struct Field_t
	{
		char* name;
		Node_t node;
		Field_t* next;
		Field_t* prev;

		// Inlines
		inline const char* GetString() const { return node.value.GetString(); };
		inline Field_t* GetNextKey() const { return next; };
		inline Field_t* GetLastKey() const { return prev; };

		inline Field_t* GetFirstSubKey() const { return node.GetFirstSubKey(); };
		inline Field_t* FindKey(const char* keyName) const { return node.FindKey(keyName); };
	};

public:
	static Node_t* LoadFromBuffer(const char* const bufName, char* const buf, eFieldType rootType);
	static Node_t* LoadFromFile(const char* const filePath, const char* const pathID = nullptr, bool* parseFailure = nullptr);
};

///////////////////////////////////////////////////////////////////////////////

RSON::Field_t* RSON::Node_t::GetFirstSubKey() const
{
	assert(type & eFieldType::RSON_OBJECT);
	return value.subKey;
};

RSON::Field_t* RSON::Node_t::FindKey(const char* const keyName) const
{
	assert(type & eFieldType::RSON_OBJECT);

	for (Field_t* key = GetFirstSubKey(); key != nullptr; key = key->GetNextKey())
	{
		if (!_stricmp(key->name, keyName))
			return key;
	}

	return NULL;
}

RSON::Value_t* RSON::Node_t::GetArrayValue(const int index) const
{
	assert(type & eFieldType::RSON_ARRAY);
	return value.GetSubValue() + index;
}

///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
inline RSON::Node_t* (*RSON_LoadFromBuffer)(const char* bufName, char* buf, RSON::eFieldType rootType, __int64 a4, void* a5);
inline void (*RSON_Free)(RSON::Node_t* rson, CAlignedMemAlloc* allocator);

///////////////////////////////////////////////////////////////////////////////
class VRSON : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("RSON_LoadFromBuffer", RSON_LoadFromBuffer);
		LogFunAdr("RSON_Free", RSON_Free);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "E8 ?? ?? ?? ?? 48 89 45 60 48 8B D8").FollowNearCallSelf().GetPtr(RSON_LoadFromBuffer);
		Module_FindPattern(g_GameDll, "E8 ?? ?? ?? ?? 48 83 EF 01 75 E7").FollowNearCallSelf().GetPtr(RSON_Free);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { }
};
///////////////////////////////////////////////////////////////////////////////

