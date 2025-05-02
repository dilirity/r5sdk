#ifndef VSCRIPT_GAMEDLL_DEFS_H
#define VSCRIPT_GAMEDLL_DEFS_H

#define SCRIPT_REGISTER_FUNC(s, binding, useTypeCompiler) s->RegisterFunction(&binding, useTypeCompiler)
#define SCRIPT_THROW_ERROR_FUNC(s, v) CSquirrelVM__ThrowError(s, v)

#endif // VSCRIPT_GAMEDLL_DEFS_H
