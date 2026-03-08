#ifndef SCRIPTREMOTEFUNCTIONS_SHARED_H
#define SCRIPTREMOTEFUNCTIONS_SHARED_H

constexpr int SCRIPT_REMOTE_SERVER_MAX_PARAMS = 8;
constexpr int SCRIPT_REMOTE_SERVER_MAX_STRING_LEN = 255;
constexpr int SCRIPT_REMOTE_SERVER_MAX_FUNCTIONS = 768;
constexpr int SCRIPT_REMOTE_FUNC_INDEX_BITS = 10;

enum class ScriptRemoteParamType_e : uint8_t
{
	SRP_INT,
	SRP_FLOAT,
	SRP_BOOL,
	SRP_STRING,
};

struct ScriptRemoteParamDesc_t
{
	ScriptRemoteParamType_e type;
	int intMin;
	int intMax;
	float floatMin;
	float floatMax;
};

struct ScriptRemoteFuncDesc_t
{
	char szName[128];
	uint16_t nIndex;
	int nParamCount;
	ScriptRemoteParamDesc_t params[SCRIPT_REMOTE_SERVER_MAX_PARAMS];
};

bool ScriptRemoteServer_RegisterFunction(const char* pszName, int nParamCount,
	const ScriptRemoteParamDesc_t* pParams);
const ScriptRemoteFuncDesc_t* ScriptRemoteServer_FindFunction(const char* pszName);
const ScriptRemoteFuncDesc_t* ScriptRemoteServer_GetFunctionByIndex(uint16_t nIndex);
int ScriptRemoteServer_GetFunctionCount();
uint32_t ScriptRemoteServer_CalcChecksum();
void ScriptRemoteServer_LockRegistrations();
void ScriptRemoteServer_ClearRegistrations();

#endif // SCRIPTREMOTEFUNCTIONS_SHARED_H
