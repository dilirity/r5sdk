#ifndef MSS_H
#define MSS_H
#include "radtypes.h"

#define MSSIO_FLAGS_DONT_CLOSE_HANDLE        (1<<0)
#define MSSIO_FLAGS_QUERY_SIZE_ONLY          (1<<1)
#define MSSIO_FLAGS_DONT_USE_OFFSET          (1<<2)
#define MSSIO_FLAGS_QUERY_START_ONLY         (1<<3)

#define MSSIO_STATUS_COMPLETE_NOP                 0
#define MSSIO_STATUS_COMPLETE                     1
#define MSSIO_STATUS_ERROR_FAILED_OPEN       0x4099
#define MSSIO_STATUS_ERROR_FAILED_READ       0x4100
#define MSSIO_STATUS_ERROR_SHUTDOWN          0x4101
#define MSSIO_STATUS_ERROR_CANCELLED         0x4102
#define MSSIO_STATUS_ERROR_MEMORY_ALLOC_FAIL 0x4103

// The original struct that was compiled with this
// version of the game as reference:
/*
struct MilesAsyncRead
{
	char FileName[256];
	size_t Offset;
	size_t Start;
	size_t Count;
	size_t LastCount;
	char Internal[40];
	U64 RequestId;
	void* Buffer;
	char gap158[24];
	const char* LastAllocSrcFileName;
	S32 LastAllocSrcFileLine;
	U16 Flags;
	U32 ReadAmt;
	S32 volatile Status;
};
*/

struct MilesAsyncRead
{
	size_t Offset;
	size_t Start;
	size_t Count;
	size_t LastCount;
	char Internal[32];
	char* FileName;
	U64 RequestId;
	void* Buffer;
	void* Driver;
	const char* LastAllocSrcFileName;
	S32 LastAllocSrcFileLine;
	U8 Flags;
	U8 Padding1;
	U8 ReadAmt;
	U8 Padding2;
	S32 volatile Status;
};

struct MilesSubFileInfo_s
{
	char const* filename;
	size_t size;
	size_t start;
};

void MilesGetSubFileInfo(char* const buf, char const* const filename, MilesSubFileInfo_s* const sfi);

#endif // MSS_H
