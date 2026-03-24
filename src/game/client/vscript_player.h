#ifndef VSCRIPT_PLAYER_H
#define VSCRIPT_PLAYER_H

struct ScriptClassDescriptor_t;

void Script_RegisterPlayerScriptFunctions(ScriptClassDescriptor_t* playerStruct);
void Script_RegisterPlayerScriptSetters(ScriptClassDescriptor_t* playerStruct);
void VScriptPlayer_LevelShutdown();

#endif // VSCRIPT_PLAYER_H
