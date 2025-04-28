#include "core/stdafx.h"
#include <tier0/memstd.h>
#include "tier1/utlbuffer.h"
#include <filesystem/filesystem.h>
#include "rtech/rson.h"

//-----------------------------------------------------------------------------
// Purpose: loads an RSON from a buffer
// Input  : *bufName - 
//          *buf     - 
//          rootType - 
// Output : pointer to RSON object on success, nullptr otherwise
//-----------------------------------------------------------------------------
RSON::Node_t* RSON::LoadFromBuffer(const char* const bufName, char* const buf, RSON::eFieldType rootType)
{
	return RSON_LoadFromBuffer(bufName, buf, rootType, 0, NULL);
}

//-----------------------------------------------------------------------------
// Purpose: loads an RSON from a file
// Input  : *filePath - 
//          *pathID   - 
// Output : pointer to RSON object on success, nullptr otherwise
//-----------------------------------------------------------------------------
RSON::Node_t* RSON::LoadFromFile(const char* const filePath, const char* const pathID, bool* const parseFailure)
{
	if (parseFailure)
		*parseFailure = false;

	FileHandle_t file = FileSystem()->Open(filePath, "rt", pathID);

	if (!file)
		return NULL;

	const ssize_t fileSize = FileSystem()->Size(file);
	std::unique_ptr<char[]> fileBuf(new char[fileSize + 1]);

	const ssize_t numRead = FileSystem()->Read(fileBuf.get(), fileSize, file);
	FileSystem()->Close(file);

	fileBuf[numRead] = '\0';
	RSON::Node_t* node = RSON::LoadFromBuffer(filePath, fileBuf.get(), eFieldType::RSON_OBJECT);
	
	if (!node && parseFailure)
		*parseFailure = true;

	return node;
}