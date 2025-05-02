#ifndef VSQUIRREL_BRIDGE_H
#define VSQUIRREL_BRIDGE_H
#include "vscript/ivscript.h"

#define SQUIRRELVM_BRIDGE_INTERFACE_VERSION "VSquirrelVMBridge001"

//-----------------------------------------------------------------------------
// Simple Squirrel API Bridge class used for exposing commonly used API's to
// external programs
//-----------------------------------------------------------------------------
class CSquirrelVMBridge : public CTier1AppSystem< ISquirrelVMBridge >
{
public:
	virtual SQRESULT RegisterFunction(CSquirrelVM* const s, ScriptFunctionBinding_t* const binding, const bool useTypeCompiler);
	virtual SQRESULT RegisterConstant(CSquirrelVM* const s, const SQChar* const name, const SQInteger value);

	virtual ScriptStatus_t ExecuteFunction(CSquirrelVM* const s, HSCRIPT hFunction, const ScriptVariant_t* const pArgs, unsigned int nArgs, ScriptVariant_t* const pReturn, HSCRIPT hScope);
	virtual bool ExecuteCodeCallback(CSquirrelVM* const s, const SQChar* const name);

	virtual bool Run(CSquirrelVM* const s, const SQChar* const script);

	virtual SQRESULT StackGetBool(HSQUIRRELVM v, const SQInteger idx, SQBool* const b);
	virtual SQRESULT StackGetInteger(HSQUIRRELVM v, const SQInteger idx, SQInteger* const i);
	virtual SQRESULT StackGetFloat(HSQUIRRELVM v, const SQInteger idx, SQFloat* const f);
	virtual SQRESULT StackGetVector(HSQUIRRELVM v, const SQInteger idx, const SQVector3D** const w);
	virtual SQRESULT StackGetThread(HSQUIRRELVM v, const SQInteger idx, HSQUIRRELVM* const thread);
	virtual SQRESULT StackGetString(HSQUIRRELVM v, const SQInteger idx, const SQChar** const c);
	virtual SQRESULT StackGetObject(HSQUIRRELVM v, const SQInteger idx, HSQOBJECT* const po);

	virtual void StackPushBool(HSQUIRRELVM v, const SQBool b);
	virtual void StackPushInteger(HSQUIRRELVM v, const SQInteger i);
	virtual void StackPushFloat(HSQUIRRELVM v, const SQFloat f);
	virtual void StackPushVector(HSQUIRRELVM v, const SQVector3D* const w);
	virtual void StackPushString(HSQUIRRELVM v, const SQChar* const string, const SQInteger len);
	virtual void StackPushObject(HSQUIRRELVM v, HSQOBJECT obj);
	virtual void StackPushNull(HSQUIRRELVM v);

	virtual SQInteger StackGetTop(HSQUIRRELVM v);
	virtual void StackSetTop(HSQUIRRELVM v, const SQInteger newtop);

	virtual void RaiseError(HSQUIRRELVM v, const SQChar* const pszFormat, ...);
	virtual bool ThrowError(CSquirrelVM* const s, HSQUIRRELVM v);
};

extern CSquirrelVMBridge g_squirrelVMBridge;
extern CSquirrelVMBridge* SquirrelVMBridge();

#endif // VSQUIRREL_BRIDGE_H
