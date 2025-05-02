#ifndef VSCRIPT_PLUGINDLL_DEFS_H
#define VSCRIPT_PLUGINDLL_DEFS_H

#define SCRIPT_REGISTER_FUNC(s, binding, useTypeCompiler) g_pSquirrelVMBridge->RegisterFunction(s, &binding, useTypeCompiler)
#define SCRIPT_THROW_ERROR_FUNC(s, v) g_pSquirrelVMBridge->ThrowError(s, v)

#endif // VSCRIPT_PLUGINDLL_DEFS_H
