#ifndef RTECH_PAKTOOLS_H
#define RTECH_PAKTOOLS_H
#include "rtech/ipakfile.h"

extern bool Pak_ReadPathExists();
extern bool Pak_WritePathExists();

extern bool Pak_CreateReadPath();
extern bool Pak_CreateWritePath();

extern int Pak_FileExists(const char* const pakFilePath);

extern const char* Pak_StatusToString(const PakStatus_e status);
const char* Pak_DecoderToString(const PakDecodeMode_e mode);

extern PakGuid_t Pak_StringToGuid(const char* const string);

extern PakLoadedInfo_s* Pak_GetPakInfo(const PakHandle_t pakId);
extern const PakLoadedInfo_s* Pak_GetPakInfo(const char* const pakName);

extern PakPatchDataHeader_s* Pak_GetPatchDataHeader(PakFileHeader_s* const pakHeader);
extern PakPatchFileHeader_s* Pak_GetPatchFileHeader(PakFileHeader_s* const pakHeader, const int index);
extern short Pak_GetPatchNumberForIndex(PakFileHeader_s* const pakHeader, const int index);

extern bool Pak_UpdatePatchHeaders(uint8_t* const inBuf, const char* const outPakFile);

extern void Pak_ShowHeaderDetails(const PakFileHeader_s* const pakHeader);

#endif // !RTECH_PAKTOOLS_H
