#ifndef TIER2_ZSTDUTILS_t
#define TIER2_ZSTDUTILS_H

#include "thirdparty/zstd/zstd.h"
#include "thirdparty/zstd/compress/zstd_compress_internal.h"
#include "thirdparty/zstd/decompress/zstd_decompress_internal.h"

struct ALIGN8 ZSTDEncoder_s
{
	ZSTDEncoder_s();
	~ZSTDEncoder_s();

	ZSTD_CCtx cctx;
};

struct ALIGN8 ZSTDDecoder_s
{
	ZSTDDecoder_s();
	~ZSTDDecoder_s();

	ZSTD_DCtx dctx;
};

#endif // TIER2_ZSTDUTILS_t
