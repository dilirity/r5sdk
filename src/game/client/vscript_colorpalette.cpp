//=============================================================================//
//
// Purpose: Color palette system with native C++ API, ConVar color bindings,
//          number formatting, slider control, and player script functions.
//
//=============================================================================//

#include "core/stdafx.h"
#include "tier1/convar.h"
#include "tier1/cvar.h"
#include "mathlib/mathlib.h"

#include "vscript/vscript.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "vscript/languages/squirrel_re/include/squirrel.h"

#include "game/shared/vscript_gamedll_defs.h"
#include "game/shared/vscript_shared.h"

#include "vscript_client.h"
#include "vscript_colorpalette.h"

#include <unordered_map>
#include <string>

//=============================================================================
// Forward declarations
//=============================================================================
static void ReticleColorChangeCB(IConVar* var, const char* pOldValue,
	float flOldValue, ChangeUserData_t pUserData);

//=============================================================================
// ConVars
//=============================================================================
static ConVar colorPalette_laserSight_minBrightness("colorPalette_laserSight_minBrightness",
	"0.5", FCVAR_RELEASE, "Minimum HSV value for laser sight color.", "0 1");
static ConVar colorPalette_laserSight_maxBrightness("colorPalette_laserSight_maxBrightness",
	"1.0", FCVAR_RELEASE, "Maximum HSV value for laser sight color.", "0 1");

static ConVar reticle_color("reticle_color", "", FCVAR_RELEASE,
	"Reticle color override as 'R G B' (0-255). Empty to reset.",
	&ReticleColorChangeCB);

static ConVar lobby_theme_color("lobby_theme_color", "199 21 11", FCVAR_RELEASE | FCVAR_ARCHIVE,
	"Lobby theme color as 'R G B' (0-255). Default: 199 21 11.");

//=============================================================================
// Color palette storage
//=============================================================================
static constexpr int MAX_PALETTE_COLORS = 256;

struct ColorPalette_t
{
	SQVector3D defaultColors[MAX_PALETTE_COLORS];
	SQVector3D overrideColors[MAX_PALETTE_COLORS];
	bool isOverridden[MAX_PALETTE_COLORS];
	std::unordered_map<std::string, int> nameToIndex;
	int nextAutoIndex;
	bool initialized;

	ColorPalette_t()
		: nextAutoIndex(0)
		, initialized(true)
	{
		for (int i = 0; i < MAX_PALETTE_COLORS; i++)
		{
			defaultColors[i].Init(255.0f, 255.0f, 255.0f);
			overrideColors[i].Init(255.0f, 255.0f, 255.0f);
			isOverridden[i] = false;
		}
	}
};

static ColorPalette_t s_colorPalette;

//=============================================================================
// Slider control value storage
//=============================================================================
static std::unordered_map<uintptr_t, float> s_sliderValues;

//=============================================================================
// Native C++ ColorPalette API
//=============================================================================

//-----------------------------------------------------------------------------
// Purpose: returns true once the color palette is ready for use
//-----------------------------------------------------------------------------
bool ColorPalette_HasBeenInitialized()
{
	return s_colorPalette.initialized;
}

//-----------------------------------------------------------------------------
// Purpose: looks up a color name and returns its palette index, or -1
//-----------------------------------------------------------------------------
int ColorPalette_GetIDFromString(const char* colorName)
{
	auto it = s_colorPalette.nameToIndex.find(colorName);
	if (it != s_colorPalette.nameToIndex.end())
		return it->second;
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: gets or assigns a palette index for a color name
//          Returns -1 if the palette is full.
//-----------------------------------------------------------------------------
static int ColorPalette_GetOrAssignID(const char* colorName)
{
	auto it = s_colorPalette.nameToIndex.find(colorName);
	if (it != s_colorPalette.nameToIndex.end())
		return it->second;

	if (s_colorPalette.nextAutoIndex >= MAX_PALETTE_COLORS)
		return -1;

	const int idx = s_colorPalette.nextAutoIndex++;
	s_colorPalette.nameToIndex[colorName] = idx;
	return idx;
}

//-----------------------------------------------------------------------------
// Purpose: gets color from palette by ID (override if set, else default)
//          Returns white (255,255,255) for out-of-range IDs.
//-----------------------------------------------------------------------------
Vector3D ColorPalette_GetColorFromID(int colorID)
{
	if (colorID < 0 || colorID >= MAX_PALETTE_COLORS)
		return Vector3D(255.0f, 255.0f, 255.0f);

	if (s_colorPalette.isOverridden[colorID])
	{
		const SQVector3D& c = s_colorPalette.overrideColors[colorID];
		return Vector3D(c.x, c.y, c.z);
	}

	const SQVector3D& c = s_colorPalette.defaultColors[colorID];
	return Vector3D(c.x, c.y, c.z);
}

//-----------------------------------------------------------------------------
// Purpose: gets the default (non-overridden) color from palette by ID
//          Always reads from the base table, ignoring overrides.
//-----------------------------------------------------------------------------
Vector3D ColorPalette_GetDefaultColorFromID(int colorID)
{
	if (colorID < 0 || colorID >= MAX_PALETTE_COLORS)
		return Vector3D(255.0f, 255.0f, 255.0f);

	const SQVector3D& c = s_colorPalette.defaultColors[colorID];
	return Vector3D(c.x, c.y, c.z);
}

//-----------------------------------------------------------------------------
// Purpose: returns true if the color at the given ID has been overridden
//-----------------------------------------------------------------------------
bool ColorPalette_IsColorCustomized(int colorID)
{
	if (colorID < 0 || colorID >= MAX_PALETTE_COLORS)
		return false;
	return s_colorPalette.isOverridden[colorID];
}

//-----------------------------------------------------------------------------
// Purpose: overrides a named color in the palette
//          Finds or assigns an index, stores the override color.
//-----------------------------------------------------------------------------
void ColorPalette_OverrideColorByName(const char* colorName, const Vector3D& color)
{
	const int idx = ColorPalette_GetOrAssignID(colorName);
	if (idx < 0)
		return;

	s_colorPalette.overrideColors[idx].Init(color.x, color.y, color.z);
	s_colorPalette.isOverridden[idx] = true;
}

//-----------------------------------------------------------------------------
// Purpose: clears the override for a named color, reverting to default
//-----------------------------------------------------------------------------
void ColorPalette_ResetColor(const char* colorName)
{
	auto it = s_colorPalette.nameToIndex.find(colorName);
	if (it == s_colorPalette.nameToIndex.end())
		return;

	s_colorPalette.isOverridden[it->second] = false;
}

//-----------------------------------------------------------------------------
// Purpose: returns the number of registered colors in the palette
//-----------------------------------------------------------------------------
int ColorPalette_GetColorCount()
{
	return s_colorPalette.nextAutoIndex;
}

//-----------------------------------------------------------------------------
// Purpose: clamps an RGB color (0-255) to minimum 50% HSV brightness
//          Native C++ version callable from ConVar callbacks and other code.
//-----------------------------------------------------------------------------
void ColorPalette_ClampAndValidateColor(Vector3D& result, const Vector3D& color)
{
	const Vector3D rgb(color.x / 255.0f, color.y / 255.0f, color.z / 255.0f);
	Vector3D hsv;
	RGBtoHSV(rgb, hsv);

	if (hsv.z < 0.5f)
		hsv.z = 0.5f;

	Vector3D rgbOut;
	HSVtoRGB(hsv, rgbOut);

	result.x = (float)Clamp((int)(rgbOut.x * 255.0f), 0, 255);
	result.y = (float)Clamp((int)(rgbOut.y * 255.0f), 0, 255);
	result.z = (float)Clamp((int)(rgbOut.z * 255.0f), 0, 255);
}

//=============================================================================
// reticle_color ConVar change callback
// Parses "R G B" string, clamps brightness, applies override on "RETICLE".
//=============================================================================
static void ReticleColorChangeCB(IConVar* var, const char* pOldValue,
	float flOldValue, ChangeUserData_t pUserData)
{
	if (!ColorPalette_HasBeenInitialized())
		return;

	ConVar* pCVar = g_pCVar->FindVar(var->GetName());
	if (!pCVar)
		return;

	const char* str = pCVar->GetString();

	// Empty string = reset to default color
	if (!str || !*str)
	{
		ColorPalette_ResetColor("RETICLE");
		return;
	}

	// Parse "R G B" space-separated
	char* end;
	float r = (float)strtod(str, &end);
	float g = (float)strtod(end, &end);
	float b = (float)strtod(end, &end);

	// Default to 100,100,100 if all components are zero
	if (r == 0.0f && g == 0.0f && b == 0.0f)
	{
		r = 100.0f;
		g = 100.0f;
		b = 100.0f;
	}

	Vector3D color(r, g, b);
	Vector3D clamped;
	ColorPalette_ClampAndValidateColor(clamped, color);
	ColorPalette_OverrideColorByName("RETICLE", clamped);
}

//=============================================================================
// Script function implementations
//=============================================================================

//-----------------------------------------------------------------------------
// Group A: Pure math color conversion
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: converts RGB (0-255) to HSV (H=0-360, S=0-1, V=0-1)
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_RGBtoHSV_Impl(HSQUIRRELVM v)
{
	const SQVector3D* rgbVec = nullptr;
	sq_getvector(v, 2, &rgbVec);

	const Vector3D rgb(rgbVec->x / 255.0f, rgbVec->y / 255.0f, rgbVec->z / 255.0f);
	Vector3D hsv;
	RGBtoHSV(rgb, hsv);

	const SQVector3D result(hsv.x, hsv.y, hsv.z);
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_RGBtoHSV(HSQUIRRELVM v) { return Script_ColorPalette_RGBtoHSV_Impl(v); }
static SQRESULT UIScript_ColorPalette_RGBtoHSV(HSQUIRRELVM v) { return Script_ColorPalette_RGBtoHSV_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: converts HSV (H=0-360, S=0-1, V=0-1) to RGB (0-255)
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_HSVtoRGB_Impl(HSQUIRRELVM v)
{
	const SQVector3D* hsvVec = nullptr;
	sq_getvector(v, 2, &hsvVec);

	const Vector3D hsv(hsvVec->x, hsvVec->y, hsvVec->z);
	Vector3D rgb;
	HSVtoRGB(hsv, rgb);

	const SQVector3D result(
		(SQFloat)(int)(rgb.x * 255.0f),
		(SQFloat)(int)(rgb.y * 255.0f),
		(SQFloat)(int)(rgb.z * 255.0f));
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_HSVtoRGB(HSQUIRRELVM v) { return Script_ColorPalette_HSVtoRGB_Impl(v); }
static SQRESULT UIScript_ColorPalette_HSVtoRGB(HSQUIRRELVM v) { return Script_ColorPalette_HSVtoRGB_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: script wrapper for ColorPalette_ClampAndValidateColor
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_ClampAndValidateColor_Impl(HSQUIRRELVM v)
{
	const SQVector3D* rgbVec = nullptr;
	sq_getvector(v, 2, &rgbVec);

	Vector3D color(rgbVec->x, rgbVec->y, rgbVec->z);
	Vector3D clamped;
	ColorPalette_ClampAndValidateColor(clamped, color);

	const SQVector3D result(clamped.x, clamped.y, clamped.z);
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_ClampAndValidateColor(HSQUIRRELVM v) { return Script_ColorPalette_ClampAndValidateColor_Impl(v); }
static SQRESULT UIScript_ColorPalette_ClampAndValidateColor(HSQUIRRELVM v) { return Script_ColorPalette_ClampAndValidateColor_Impl(v); }

//-----------------------------------------------------------------------------
// Group B: ConVar-based color functions
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: clamps color brightness using ConVar min/max for laser sights
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_ClampAndValidateLaserSightColor_Impl(HSQUIRRELVM v)
{
	const SQVector3D* rgbVec = nullptr;
	sq_getvector(v, 2, &rgbVec);

	const float minBrightness = colorPalette_laserSight_minBrightness.GetFloat();
	const float maxBrightness = colorPalette_laserSight_maxBrightness.GetFloat();

	const Vector3D rgb(rgbVec->x / 255.0f, rgbVec->y / 255.0f, rgbVec->z / 255.0f);
	Vector3D hsv;
	RGBtoHSV(rgb, hsv);

	hsv.z = Clamp(hsv.z, minBrightness, maxBrightness);

	Vector3D rgbOut;
	HSVtoRGB(hsv, rgbOut);

	const SQVector3D result(
		(SQFloat)Clamp((int)(rgbOut.x * 255.0f), 0, 255),
		(SQFloat)Clamp((int)(rgbOut.y * 255.0f), 0, 255),
		(SQFloat)Clamp((int)(rgbOut.z * 255.0f), 0, 255));
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_ClampAndValidateLaserSightColor(HSQUIRRELVM v) { return Script_ColorPalette_ClampAndValidateLaserSightColor_Impl(v); }
static SQRESULT UIScript_ColorPalette_ClampAndValidateLaserSightColor(HSQUIRRELVM v) { return Script_ColorPalette_ClampAndValidateLaserSightColor_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: reads a ConVar as a color vector (0-255 RGB)
//-----------------------------------------------------------------------------
static SQRESULT Script_GetConVarColor_Impl(HSQUIRRELVM v)
{
	const SQChar* cvarName = nullptr;
	sq_getstring(v, 2, &cvarName);

	ConVar* pVar = g_pCVar->FindVar(cvarName);
	if (!pVar)
	{
		v_SQVM_ScriptError("ConVar '%s' not found", cvarName);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	if (pVar->IsFlagSet(FCVAR_PROTECTED))
	{
		v_SQVM_ScriptError("ConVar '%s' is protected", cvarName);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	if (pVar->IsFlagSet(FCVAR_NEVER_AS_STRING))
	{
		v_SQVM_ScriptError("ConVar '%s' is FCVAR_NEVER_AS_STRING", cvarName);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	const Color c = pVar->GetColor();
	const SQVector3D result((SQFloat)c.r(), (SQFloat)c.g(), (SQFloat)c.b());
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_GetConVarColor(HSQUIRRELVM v) { return Script_GetConVarColor_Impl(v); }
static SQRESULT UIScript_GetConVarColor(HSQUIRRELVM v) { return Script_GetConVarColor_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: sets a ConVar from a color vector (0-255 RGB)
//-----------------------------------------------------------------------------
static SQRESULT Script_SetConVarColor_Impl(HSQUIRRELVM v)
{
	const SQChar* cvarName = nullptr;
	sq_getstring(v, 2, &cvarName);

	const SQVector3D* colorVec = nullptr;
	sq_getvector(v, 3, &colorVec);

	ConVar* pVar = g_pCVar->FindVar(cvarName);
	if (!pVar)
	{
		v_SQVM_ScriptError("ConVar '%s' not found", cvarName);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	if (pVar->IsFlagSet(FCVAR_PROTECTED))
	{
		v_SQVM_ScriptError("ConVar '%s' is protected", cvarName);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	if (pVar->IsFlagSet(FCVAR_NEVER_AS_STRING))
	{
		v_SQVM_ScriptError("ConVar '%s' is FCVAR_NEVER_AS_STRING", cvarName);
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	const Color c(
		Clamp((int)colorVec->x, 0, 255),
		Clamp((int)colorVec->y, 0, 255),
		Clamp((int)colorVec->z, 0, 255),
		255);
	pVar->SetValue(c);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_SetConVarColor(HSQUIRRELVM v) { return Script_SetConVarColor_Impl(v); }
static SQRESULT UIScript_SetConVarColor(HSQUIRRELVM v) { return Script_SetConVarColor_Impl(v); }

//-----------------------------------------------------------------------------
// Group C: Color palette script functions
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: script wrapper - gets default (non-overridden) color from palette
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_GetDefaultColorFromID_Impl(HSQUIRRELVM v)
{
	SQInteger colorID = 0;
	sq_getinteger(v, 2, &colorID);

	const Vector3D color = ColorPalette_GetDefaultColorFromID((int)colorID);
	const SQVector3D result(color.x, color.y, color.z);
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_GetDefaultColorFromID(HSQUIRRELVM v) { return Script_ColorPalette_GetDefaultColorFromID_Impl(v); }
static SQRESULT UIScript_ColorPalette_GetDefaultColorFromID(HSQUIRRELVM v) { return Script_ColorPalette_GetDefaultColorFromID_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: script wrapper - gets color from palette (override if set)
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_GetColorFromID_Impl(HSQUIRRELVM v)
{
	SQInteger colorID = 0;
	sq_getinteger(v, 2, &colorID);

	const Vector3D color = ColorPalette_GetColorFromID((int)colorID);
	const SQVector3D result(color.x, color.y, color.z);
	sq_pushvector(v, &result);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_GetColorFromID(HSQUIRRELVM v) { return Script_ColorPalette_GetColorFromID_Impl(v); }
static SQRESULT UIScript_ColorPalette_GetColorFromID(HSQUIRRELVM v) { return Script_ColorPalette_GetColorFromID_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: script wrapper - gets palette index from color name
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_GetIDFromString_Impl(HSQUIRRELVM v)
{
	const SQChar* colorName = nullptr;
	sq_getstring(v, 2, &colorName);

	sq_pushinteger(v, ColorPalette_GetIDFromString(colorName));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_GetIDFromString(HSQUIRRELVM v) { return Script_ColorPalette_GetIDFromString_Impl(v); }
static SQRESULT UIScript_ColorPalette_GetIDFromString(HSQUIRRELVM v) { return Script_ColorPalette_GetIDFromString_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: script wrapper - checks if a palette color has been overridden
//-----------------------------------------------------------------------------
static SQRESULT Script_ColorPalette_IsColorCustomized_Impl(HSQUIRRELVM v)
{
	SQInteger colorID = 0;
	sq_getinteger(v, 2, &colorID);

	sq_pushbool(v, ColorPalette_IsColorCustomized((int)colorID));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_ColorPalette_IsColorCustomized(HSQUIRRELVM v) { return Script_ColorPalette_IsColorCustomized_Impl(v); }
static SQRESULT UIScript_ColorPalette_IsColorCustomized(HSQUIRRELVM v) { return Script_ColorPalette_IsColorCustomized_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: stores a named color override in the color palette (UI context)
//-----------------------------------------------------------------------------
static SQRESULT UIScript_OverrideColor(HSQUIRRELVM v)
{
	const SQVector3D* colorVec = nullptr;
	sq_getvector(v, 2, &colorVec);

	const SQChar* colorName = nullptr;
	sq_getstring(v, 3, &colorName);

	const Vector3D color(colorVec->x, colorVec->y, colorVec->z);
	ColorPalette_OverrideColorByName(colorName, color);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Group D: HUD slider control
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: sets slider control value for a HUD element
//          Since SliderControl doesn't exist in r5sdk, we provide
//          map-based storage keyed by entity handle.
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_SliderControl_SetCurrentValue_Impl(HSQUIRRELVM v)
{
	HSQOBJECT entityObj;
	sq_getstackobj(v, 2, &entityObj);

	SQFloat value = 0.0f;
	sq_getfloat(v, 3, &value);

	const uintptr_t key = (uintptr_t)entityObj._unVal.pUserPointer;
	s_sliderValues[key] = static_cast<float>(value);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_SliderControl_SetCurrentValue(HSQUIRRELVM v) { return Script_Hud_SliderControl_SetCurrentValue_Impl(v); }
static SQRESULT UIScript_Hud_SliderControl_SetCurrentValue(HSQUIRRELVM v) { return Script_Hud_SliderControl_SetCurrentValue_Impl(v); }

//-----------------------------------------------------------------------------
// Purpose: gets slider control value from a HUD element
//          Returns the last value set via SetCurrentValue, or 0.0f if none.
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_SliderControl_GetCurrentValue_Impl(HSQUIRRELVM v)
{
	HSQOBJECT entityObj;
	sq_getstackobj(v, 2, &entityObj);

	const uintptr_t key = (uintptr_t)entityObj._unVal.pUserPointer;
	auto it = s_sliderValues.find(key);

	sq_pushfloat(v, it != s_sliderValues.end() ? it->second : 0.0f);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_SliderControl_GetCurrentValue(HSQUIRRELVM v) { return Script_Hud_SliderControl_GetCurrentValue_Impl(v); }
static SQRESULT UIScript_Hud_SliderControl_GetCurrentValue(HSQUIRRELVM v) { return Script_Hud_SliderControl_GetCurrentValue_Impl(v); }

//-----------------------------------------------------------------------------
// Group E: Number formatting
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: converts RUI-style format string to printf format
//          RUI format uses example-based templates:
//            '1'   = number placeholder
//            '0'   = zero padding before '1'
//            '_'   = space padding before '1'
//            '+'   = force sign display
//            '.'   = decimal separator
//            '00'  = fixed decimal places after '.'
//            '99'  = trimmed trailing zeros after '.'
//          Examples: "001.00" -> "%06.2f", "+1" -> "%+d", "__1.99" -> "%5.2f"
//-----------------------------------------------------------------------------
static void ConvertRuiStyleFormatToPrintfFormat(const char* ruiFormat, char* out,
	int outSize, bool isInteger, bool* trimTrailingZeros)
{
	*trimTrailingZeros = false;

	if (!ruiFormat || !*ruiFormat)
	{
		snprintf(out, outSize, isInteger ? "%%d" : "%%f");
		return;
	}

	const char* p = ruiFormat;

	// Phase 1: Parse prefix flags
	bool forceSign = false;
	int spacePad = 0;

	if (*p == '+') { forceSign = true; p++; }
	while (*p == '_') { spacePad++; p++; }

	// Count leading zeros
	int zeroPad = 0;
	while (*p == '0') { zeroPad++; p++; }

	// Skip the example digit '1'
	if (*p == '1') p++;

	const int intWidth = spacePad + zeroPad + 1;

	// Phase 2: Parse decimal part
	int fracDigits = 0;
	bool hasFrac = false;

	if (*p == '.')
	{
		hasFrac = true;
		p++;
		const char* fracStart = p;
		while (*p == '0' || *p == '9') { fracDigits++; p++; }

		// All '9's after dot means trim trailing zeros
		if (fracDigits > 0)
		{
			bool allNines = true;
			for (int i = 0; i < fracDigits; i++)
			{
				if (fracStart[i] != '9') { allNines = false; break; }
			}
			*trimTrailingZeros = allNines;
		}
	}

	// Phase 3: Build printf format string
	char flags[4] = {0};
	int fi = 0;
	if (forceSign) flags[fi++] = '+';
	if (zeroPad > 0) flags[fi++] = '0';

	if (hasFrac || !isInteger)
	{
		const int totalWidth = (intWidth > 1 || zeroPad > 0)
			? intWidth + 1 + fracDigits : 0;

		if (totalWidth > 0)
			snprintf(out, outSize, "%%%s%d.%df", flags, totalWidth, fracDigits);
		else
			snprintf(out, outSize, "%%%s.%df", flags, fracDigits);
	}
	else
	{
		if (intWidth > 1)
			snprintf(out, outSize, "%%%s%dd", flags, intWidth);
		else
			snprintf(out, outSize, "%%%sd", flags);
	}
}

//-----------------------------------------------------------------------------
// Purpose: strips trailing zeros after the decimal point
//          "3.500" -> "3.5", "3.000" -> "3", "3.0" -> "3"
//-----------------------------------------------------------------------------
static void TrimTrailingZeros(char* buffer)
{
	char* dot = strchr(buffer, '.');
	if (!dot) return;

	char* end = buffer + strlen(buffer) - 1;
	while (end > dot && *end == '0') end--;

	if (end == dot)
		*end = '\0';
	else
		*(end + 1) = '\0';
}

//-----------------------------------------------------------------------------
// Purpose: inserts thousands separators into a number string
//-----------------------------------------------------------------------------
static void InsertThousandsSeparators(char* buffer, int bufferSize)
{
	char* dot = strchr(buffer, '.');
	char* intEnd = dot ? dot : buffer + strlen(buffer);

	// Account for sign character
	int startPos = 0;
	if (buffer[0] == '-' || buffer[0] == '+' || buffer[0] == ' ')
		startPos = 1;

	const int intLen = (int)(intEnd - buffer);
	const int digitCount = intLen - startPos;

	if (digitCount <= 3)
		return;

	const int numSeps = (digitCount - 1) / 3;
	const int newLen = (int)strlen(buffer) + numSeps;

	if (newLen >= bufferSize - 1)
		return;

	const int tailLen = (int)strlen(intEnd);
	memmove(intEnd + numSeps, intEnd, tailLen + 1);

	int src = intLen - 1;
	int dst = intLen + numSeps - 1;
	int count = 0;

	while (src >= startPos)
	{
		buffer[dst--] = buffer[src--];
		count++;
		if (count == 3 && src >= startPos)
		{
			buffer[dst--] = ',';
			count = 0;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: formats and localizes a number for UI display
//          Shared between CLIENT and UI contexts
//-----------------------------------------------------------------------------
static SQRESULT FormatAndLocalizeNumber_Impl(HSQUIRRELVM v)
{
	const SQChar* formatStr = nullptr;
	sq_getstring(v, 2, &formatStr);

	HSQOBJECT obj;
	sq_getstackobj(v, 3, &obj);

	SQBool addThousands = SQFalse;
	sq_getbool(v, 4, &addThousands);

	const bool isInteger = (obj._type == OT_INTEGER);

	bool trimZeros = false;
	char printfFmt[64];
	ConvertRuiStyleFormatToPrintfFormat(formatStr, printfFmt, sizeof(printfFmt),
		isInteger, &trimZeros);

	char buffer[256];

	if (isInteger)
	{
		SQInteger intVal = 0;
		sq_getinteger(v, 3, &intVal);

		if (strchr(printfFmt, 'f'))
			snprintf(buffer, sizeof(buffer), printfFmt, (double)intVal);
		else
			snprintf(buffer, sizeof(buffer), printfFmt, (int)intVal);
	}
	else
	{
		SQFloat floatVal = 0.0f;
		sq_getfloat(v, 3, &floatVal);

		if (strchr(printfFmt, 'd'))
			snprintf(buffer, sizeof(buffer), printfFmt, (int)floatVal);
		else
			snprintf(buffer, sizeof(buffer), printfFmt, (double)floatVal);
	}

	if (trimZeros)
		TrimTrailingZeros(buffer);

	if (addThousands)
		InsertThousandsSeparators(buffer, sizeof(buffer));

	sq_pushstring(v, buffer, -1);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT ClientScript_FormatAndLocalizeNumber(HSQUIRRELVM v)
{
	return FormatAndLocalizeNumber_Impl(v);
}

static SQRESULT UIScript_FormatAndLocalizeNumber(HSQUIRRELVM v)
{
	return FormatAndLocalizeNumber_Impl(v);
}

//-----------------------------------------------------------------------------
// Group F: Player functions
//-----------------------------------------------------------------------------

static constexpr int CPLAYER_M_PLAYERFLAGS_OFFSET = 0x2CAC;

//-----------------------------------------------------------------------------
// Purpose: returns true if the player's connection is active
//          Checks bit 1 (0x2) of m_playerFlags; connection is active when
//          the bit is NOT set.
//-----------------------------------------------------------------------------
static SQRESULT Script_IsConnectionActive(HSQUIRRELVM v)
{
	void* pPlayer = nullptr;

	if (!v_sq_getentity(v, reinterpret_cast<SQEntity*>(&pPlayer)))
		return SQ_ERROR;

	const int playerFlags = *reinterpret_cast<int*>(
		reinterpret_cast<char*>(pPlayer) + CPLAYER_M_PLAYERFLAGS_OFFSET);

	sq_pushbool(v, (playerFlags & 2) == 0);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//=============================================================================
// HUD element helpers
//=============================================================================

// CClientScriptHudElement layout offsets
static constexpr ptrdiff_t HUDELEMENT_PANEL_OFFSET   = 0x28;
static constexpr ptrdiff_t HUDELEMENT_SQOBJECT_OFFSET = 0x30;
static constexpr ptrdiff_t HUDELEMENT_OWNER_OFFSET    = 0x38;

// VGUI Panel vtable offsets
static constexpr ptrdiff_t PANEL_VT_GETNAME           = 0xC0;  // GetClientPanelName
static constexpr ptrdiff_t PANEL_VT_ISBUTTON          = 0x610; // IsButton
static constexpr ptrdiff_t PANEL_VT_ISLABEL           = 0x608; // IsLabel
static constexpr ptrdiff_t PANEL_VT_ISDIALOGLISTBTN   = 0x618; // IsDialogListButton
static constexpr ptrdiff_t PANEL_VT_ISTEXTENTRY       = 0x648; // IsTextEntry
static constexpr ptrdiff_t PANEL_VT_ISGRIDBTNLIST     = 0x660; // IsGridButtonListPanel
static constexpr ptrdiff_t PANEL_VT_ISCURSOROVER      = 0x2B0; // IsCursorOver
static constexpr ptrdiff_t PANEL_VT_SETTEXTHIDDEN     = 0x8E0; // SetTextHidden(bool)
static constexpr ptrdiff_t PANEL_VT_GETTEXTHIDDEN     = 0x8E8; // GetTextHidden() -> bool
static constexpr ptrdiff_t PANEL_VT_ISSELECTED        = 0x8F8; // IsSelected
static constexpr ptrdiff_t PANEL_VT_SETBUTTONSTATE    = 0x9A8; // SetButtonState(stateId, value)

// SetButtonState state IDs
static constexpr int BTNSTATE_LOCKED  = 5;
static constexpr int BTNSTATE_NEW     = 6;
static constexpr int BTNSTATE_CHECKED = 7;

static constexpr ptrdiff_t BUTTON_STATE_BITMASK = 0x460;

// GridButtonListPanel field offsets
static constexpr ptrdiff_t GRID_BUTTONS_ARRAY  = 0x2D8; // m_Buttons.m_Memory (ptr to ptr array)
static constexpr ptrdiff_t GRID_BUTTONS_COUNT  = 0x2F0; // m_Buttons.m_Size (int)
static constexpr ptrdiff_t GRID_SCROLL_PANEL   = 0x2F8; // m_pScrollPanel (EditablePanel*, NOT an int!)
static constexpr ptrdiff_t GRID_SCROLL_BAR     = 0x300; // m_pScrollBar (ScrollBar*)

// ScrollBar vtable offsets
static constexpr ptrdiff_t SCROLLBAR_VT_SETVALUE = 0x778; // ScrollBar::SetValue(int) - vtable[239]
static constexpr ptrdiff_t SCROLLBAR_VT_GETVALUE = 0x780; // ScrollBar::GetValue() -> int - vtable[240]

static constexpr ptrdiff_t TEXTENTRY_HIDDEN    = 0x495;

// TextEntry vtable offsets
static constexpr ptrdiff_t PANEL_VT_TEXTENTRY_GETTEXT_W = 0x788;
static constexpr ptrdiff_t PANEL_VT_BUTTON_GETTEXT_W    = 0x778;

// CClientScriptHudElement event handler offsets
static constexpr ptrdiff_t HUDELEMENT_EVENTHANDLERS = 0x168;
static constexpr int MAX_EVENT_TYPE = 6;
static constexpr int EVENT_HANDLER_STRIDE = 32; // bytes per event type

//-----------------------------------------------------------------------------
// Purpose: resolves a HUD element from the Squirrel VM stack
//          Uses the engine's native resolution function sub_141056980
//          Returns CClientScriptHudElement* or nullptr
//-----------------------------------------------------------------------------
typedef uintptr_t(__fastcall* GetHudElementFn)(uintptr_t, uintptr_t, uintptr_t);
static GetHudElementFn s_fnGetHudElement = nullptr;
static uintptr_t s_hudElemTypeDesc = 0;

static void FixButtonStateArgNameTable(uintptr_t base)
{
	static const char* s_szIsChecked = "isChecked";

	// Button state arg name table; entry 7 is uninitialized — patch to "isChecked"
	uintptr_t* table = reinterpret_cast<uintptr_t*>(base + 0x131A9D0);

	const char* entry2 = reinterpret_cast<const char*>(table[2]);
	if (!entry2 || strcmp(entry2, "isDisabled") != 0)
	{
		Warning(eDLL_T::CLIENT, "ButtonState: table validation failed at entry 2\n");
		return;
	}

	DWORD oldProtect;
	VirtualProtect(&table[7], sizeof(uintptr_t), PAGE_READWRITE, &oldProtect);
	table[7] = reinterpret_cast<uintptr_t>(s_szIsChecked);
	VirtualProtect(&table[7], sizeof(uintptr_t), oldProtect, &oldProtect);

	Msg(eDLL_T::CLIENT, "ButtonState: patched arg name table entry 7 -> \"%s\"\n",
		s_szIsChecked);
}

static void InitHudElementResolver()
{
	static bool s_initialized = false;
	if (s_initialized)
		return;
	s_initialized = true;

	const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
	s_fnGetHudElement = reinterpret_cast<GetHudElementFn>(base + 0x1056980);
	s_hudElemTypeDesc = base + 0x2337A70;

	FixButtonStateArgNameTable(base);
}

static uintptr_t GetHudElement(HSQUIRRELVM v)
{
	InitHudElementResolver();
	if (!s_fnGetHudElement || !s_hudElemTypeDesc)
		return 0;
	// arg2 must be a pointer to the SQObject for the hud element (stack param 2),
	// NOT the SQVM pointer. The native function dereferences it as an SQObject.
	SQObjectPtr& obj = stack_get(v, 2);
	return s_fnGetHudElement(reinterpret_cast<uintptr_t>(v),
		reinterpret_cast<uintptr_t>(&obj), s_hudElemTypeDesc);
}

// Helper: get the VGUI panel pointer from a CClientScriptHudElement
static uintptr_t GetPanel(uintptr_t hudElem)
{
	return *reinterpret_cast<uintptr_t*>(hudElem + HUDELEMENT_PANEL_OFFSET);
}

// Helper: call a bool-returning vtable function on a panel
static bool PanelVtableBool(uintptr_t panel, ptrdiff_t vtOffset)
{
	uintptr_t vtable = *reinterpret_cast<uintptr_t*>(panel);
	typedef char(__fastcall* VtFn)(uintptr_t);
	return reinterpret_cast<VtFn>(*reinterpret_cast<uintptr_t*>(vtable + vtOffset))(panel) != 0;
}

// Helper: get panel name string for error messages
static const char* GetPanelName(uintptr_t panel)
{
	uintptr_t vtable = *reinterpret_cast<uintptr_t*>(panel);
	typedef const char*(__fastcall* GetNameFn)(uintptr_t);
	return reinterpret_cast<GetNameFn>(*reinterpret_cast<uintptr_t*>(vtable + PANEL_VT_GETNAME))(panel);
}

// Helper: get CClientScriptHud (owner) from HudElement
static uintptr_t GetHudOwner(uintptr_t hudElem)
{
	return *reinterpret_cast<uintptr_t*>(hudElem + HUDELEMENT_OWNER_OFFSET);
}

// CClientScriptHud::CreateHudElementForPanel
typedef uintptr_t(__fastcall* CreateHudElemForPanelFn)(uintptr_t, uintptr_t);
static CreateHudElemForPanelFn s_fnCreateHudElemForPanel = nullptr;

static void InitCreateHudElemForPanel()
{
	static bool s_initialized = false;
	if (s_initialized)
		return;
	s_initialized = true;

	const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
	s_fnCreateHudElemForPanel = reinterpret_cast<CreateHudElemForPanelFn>(base + 0x98E380);
}

// DialogListButton::RemoveAllListItems
typedef void(__fastcall* RemoveAllListItemsFn)(uintptr_t);
static RemoveAllListItemsFn s_fnRemoveAllListItems = nullptr;

static void InitRemoveAllListItems()
{
	static bool s_initialized = false;
	if (s_initialized)
		return;
	s_initialized = true;

	// DialogListButton::RemoveAllListItems
	CMemory result = Module_FindPattern(g_GameDll,
		"40 53 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 33 D2 48 8B CB E8");
	if (result)
		s_fnRemoveAllListItems = result.RCast<RemoveAllListItemsFn>();
}

//=============================================================================
// HUD script function implementations
//=============================================================================

//-----------------------------------------------------------------------------
// Hud_GetButtonCount(var hudElement) -> int
// Returns the button count of a GridButtonListPanel, or -1 on error
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_GetButtonCount_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISGRIDBTNLIST))
	{
		v_SQVM_ScriptError("No GridButtonListPanel element with name '%s'.", GetPanelName(panel));
		sq_pushinteger(v, -1);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	const int count = *reinterpret_cast<int*>(panel + GRID_BUTTONS_COUNT);
	sq_pushinteger(v, count);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_GetButtonCount(HSQUIRRELVM v) { return Script_Hud_GetButtonCount_Impl(v); }
static SQRESULT UIScript_Hud_GetButtonCount(HSQUIRRELVM v) { return Script_Hud_GetButtonCount_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_GetButton(var hudElement, int index) -> var
// Returns the button at the given index from a GridButtonListPanel
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_GetButton_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	SQInteger index = 0;
	sq_getinteger(v, 3, &index);

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISGRIDBTNLIST))
	{
		v_SQVM_ScriptError("No GridButtonListPanel element with name '%s'.", GetPanelName(panel));
		sq_pushnull(v);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	const int count = *reinterpret_cast<int*>(panel + GRID_BUTTONS_COUNT);
	if (index < 0 || index >= count)
	{
		v_SQVM_ScriptError("GridButtonListPanel '%s' does not have a button at index '%i'.",
			GetPanelName(panel), (int)index);
		sq_pushnull(v);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Button array: array of panel pointers at GRID_BUTTONS_ARRAY
	uintptr_t* buttonArray = *reinterpret_cast<uintptr_t**>(panel + GRID_BUTTONS_ARRAY);
	uintptr_t buttonPanel = buttonArray[index];

	if (!buttonPanel)
	{
		sq_pushnull(v);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Create/find HudElement for this button panel and return its script handle
	InitCreateHudElemForPanel();
	uintptr_t hudOwner = GetHudOwner(hudElem);
	if (s_fnCreateHudElemForPanel && hudOwner)
	{
		uintptr_t buttonHudElem = s_fnCreateHudElemForPanel(hudOwner, buttonPanel);
		if (buttonHudElem)
		{
			// hudElement+0x30 contains a POINTER to an SQObject (allocated by sub_141056680)
			SQObject* storedObj = *reinterpret_cast<SQObject**>(buttonHudElem + HUDELEMENT_SQOBJECT_OFFSET);
			if (storedObj && storedObj->_type == OT_INSTANCE && storedObj->_unVal.pInstance)
			{
				sq_pushobject(v, *storedObj);
				SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
			}
		}
	}

	sq_pushnull(v);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_GetButton(HSQUIRRELVM v) { return Script_Hud_GetButton_Impl(v); }
static SQRESULT UIScript_Hud_GetButton(HSQUIRRELVM v) { return Script_Hud_GetButton_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_GetSelectedButton(var hudElement) -> var
// Returns the currently selected button from a GridButtonListPanel
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_GetSelectedButton_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISGRIDBTNLIST))
	{
		v_SQVM_ScriptError("No GridButtonListPanel element with name '%s'.", GetPanelName(panel));
		sq_pushnull(v);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Iterate buttons looking for the selected one
	const int count = *reinterpret_cast<int*>(panel + GRID_BUTTONS_COUNT);
	uintptr_t* buttonArray = *reinterpret_cast<uintptr_t**>(panel + GRID_BUTTONS_ARRAY);

	for (int i = 0; i < count; i++)
	{
		uintptr_t btn = buttonArray[i];
		if (btn && PanelVtableBool(btn, PANEL_VT_ISSELECTED))
		{
			InitCreateHudElemForPanel();
			uintptr_t hudOwner = GetHudOwner(hudElem);
			if (s_fnCreateHudElemForPanel && hudOwner)
			{
				uintptr_t btnHudElem = s_fnCreateHudElemForPanel(hudOwner, btn);
				if (btnHudElem)
				{
					// hudElement+0x30 contains a POINTER to an SQObject (allocated by sub_141056680)
					SQObject* storedObj = *reinterpret_cast<SQObject**>(btnHudElem + HUDELEMENT_SQOBJECT_OFFSET);
					if (storedObj && storedObj->_type == OT_INSTANCE && storedObj->_unVal.pInstance)
					{
						sq_pushobject(v, *storedObj);
						SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
					}
				}
			}
		}
	}

	sq_pushnull(v);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_GetSelectedButton(HSQUIRRELVM v) { return Script_Hud_GetSelectedButton_Impl(v); }
static SQRESULT UIScript_Hud_GetSelectedButton(HSQUIRRELVM v) { return Script_Hud_GetSelectedButton_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_SetChecked(var hudElement, bool checked)
// Writes the checked bit directly to avoid SetButtonState listener overflow.
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_SetChecked_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	SQBool checked = SQFalse;
	sq_getbool(v, 3, &checked);

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISBUTTON))
	{
		v_SQVM_ScriptError("Hud element is not a button. (%s)", GetPanelName(panel));
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	DWORD* bitmask = reinterpret_cast<DWORD*>(panel + BUTTON_STATE_BITMASK);
	if (checked)
		*bitmask |= (1 << BTNSTATE_CHECKED);
	else
		*bitmask &= ~(1 << BTNSTATE_CHECKED);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_SetChecked(HSQUIRRELVM v) { return Script_Hud_SetChecked_Impl(v); }
static SQRESULT UIScript_Hud_SetChecked(HSQUIRRELVM v) { return Script_Hud_SetChecked_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_IsChecked(var hudElement) -> bool
// Returns whether a button element has the checked state set.
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_IsChecked_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISBUTTON))
	{
		sq_pushbool(v, SQFalse);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	DWORD bitmask = *reinterpret_cast<DWORD*>(panel + BUTTON_STATE_BITMASK);
	sq_pushbool(v, (bitmask & (1 << BTNSTATE_CHECKED)) ? SQTrue : SQFalse);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_IsChecked(HSQUIRRELVM v) { return Script_Hud_IsChecked_Impl(v); }
static SQRESULT UIScript_Hud_IsChecked(HSQUIRRELVM v) { return Script_Hud_IsChecked_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_ClearEventHandlers(var hudElement, int eventType)
// Clears all script event handlers for the given event type on a HUD element
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_ClearEventHandlers_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	SQInteger eventType = 0;
	sq_getinteger(v, 3, &eventType);

	if (eventType < 0 || eventType > MAX_EVENT_TYPE)
	{
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Zero out the event handler slots for this event type
	// Each event type has EVENT_HANDLER_STRIDE bytes at HUDELEMENT_EVENTHANDLERS + eventType * stride
	uintptr_t handlerBase = hudElem + HUDELEMENT_EVENTHANDLERS + eventType * EVENT_HANDLER_STRIDE;
	memset(reinterpret_cast<void*>(handlerBase), 0, EVENT_HANDLER_STRIDE);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_ClearEventHandlers(HSQUIRRELVM v) { return Script_Hud_ClearEventHandlers_Impl(v); }
static SQRESULT UIScript_Hud_ClearEventHandlers(HSQUIRRELVM v) { return Script_Hud_ClearEventHandlers_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_GetTextHidden(var hudElement) -> bool
// Returns whether text is hidden on a TextEntry panel
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_GetTextHidden_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISTEXTENTRY))
	{
		v_SQVM_ScriptError("No text entry with name '%s'.", GetPanelName(panel));
		sq_pushbool(v, SQFalse);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Read textHidden byte directly from panel struct
	const bool hidden = *reinterpret_cast<uint8_t*>(panel + TEXTENTRY_HIDDEN) != 0;
	sq_pushbool(v, hidden ? SQTrue : SQFalse);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_GetTextHidden(HSQUIRRELVM v) { return Script_Hud_GetTextHidden_Impl(v); }
static SQRESULT UIScript_Hud_GetTextHidden(HSQUIRRELVM v) { return Script_Hud_GetTextHidden_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_SetTextHidden(var hudElement, bool hidden)
// Direct field write; vtable call uses an invalid RUI arg lookup.
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_SetTextHidden_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	SQBool hidden = SQFalse;
	sq_getbool(v, 3, &hidden);

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISTEXTENTRY))
	{
		v_SQVM_ScriptError("No text entry with name '%s'.", GetPanelName(panel));
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Direct field write; vtable SetTextHidden (0x8E0) uses an invalid RUI arg lookup
	*reinterpret_cast<uint8_t*>(panel + TEXTENTRY_HIDDEN) = hidden ? 1 : 0;

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_SetTextHidden(HSQUIRRELVM v) { return Script_Hud_SetTextHidden_Impl(v); }
static SQRESULT UIScript_Hud_SetTextHidden(HSQUIRRELVM v) { return Script_Hud_SetTextHidden_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_ScrollToTop(var hudElement)
// Scrolls a GridButtonListPanel to the top by calling ScrollBar::SetValue(0)
//
// BUG FIX: Previously wrote int(0) to offset 0x2F8, which is the ScrollPanel
// POINTER (not a scroll offset). This zeroed the lower 32 bits of the 64-bit
// pointer, corrupting it to e.g. 0x0000027000000000. The next call to
// Hud_InitGridButtons would read this corrupted pointer as the parent for new
// GridButtons, crashing in vgui::Panel constructor when dereferencing vtable=1.
//
// The actual scroll position lives in the ScrollBar widget at offset 0x300.
// ScrollToItem calls ScrollBar::SetValue() at vtable[239] and GetValue() at [240].
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_ScrollToTop_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISGRIDBTNLIST))
	{
		v_SQVM_ScriptError("No GridButtonListPanel element with name '%s'.", GetPanelName(panel));
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Get the ScrollBar at offset 0x300 and call SetValue(0) via vtable[239]
	uintptr_t scrollBar = *reinterpret_cast<uintptr_t*>(panel + GRID_SCROLL_BAR);
	if (scrollBar)
	{
		uintptr_t scrollBarVtable = *reinterpret_cast<uintptr_t*>(scrollBar);
		auto SetValue = reinterpret_cast<void(__fastcall*)(uintptr_t, int)>(
			*reinterpret_cast<uintptr_t*>(scrollBarVtable + SCROLLBAR_VT_SETVALUE));
		SetValue(scrollBar, 0);
	}

	// Trigger layout refresh
	uintptr_t vtable = *reinterpret_cast<uintptr_t*>(panel);
	auto InvalidateLayout = reinterpret_cast<void(__fastcall*)(uintptr_t)>(
		*reinterpret_cast<uintptr_t*>(vtable + 0x28)); // vtable[5] = InvalidateLayout
	InvalidateLayout(panel);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_ScrollToTop(HSQUIRRELVM v) { return Script_Hud_ScrollToTop_Impl(v); }
static SQRESULT UIScript_Hud_ScrollToTop(HSQUIRRELVM v) { return Script_Hud_ScrollToTop_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_IsCursorOver(var hudElement) -> bool
// Returns whether the cursor is over a HUD panel
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_IsCursorOver_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	const bool over = PanelVtableBool(panel, PANEL_VT_ISCURSOROVER);
	sq_pushbool(v, over ? SQTrue : SQFalse);
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_IsCursorOver(HSQUIRRELVM v) { return Script_Hud_IsCursorOver_Impl(v); }
static SQRESULT UIScript_Hud_IsCursorOver(HSQUIRRELVM v) { return Script_Hud_IsCursorOver_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_DialogList_ClearList(var hudElement)
// Removes all items from a DialogListButton
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_DialogList_ClearList_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISDIALOGLISTBTN))
	{
		v_SQVM_ScriptError("No DialogListButton element with name '%s'.", GetPanelName(panel));
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	InitRemoveAllListItems();
	if (s_fnRemoveAllListItems)
		s_fnRemoveAllListItems(panel);

	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_DialogList_ClearList(HSQUIRRELVM v) { return Script_Hud_DialogList_ClearList_Impl(v); }
static SQRESULT UIScript_Hud_DialogList_ClearList(HSQUIRRELVM v) { return Script_Hud_DialogList_ClearList_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_GetUnicodeLen(var hudElement) -> int
// Returns the unicode length of text in a TextEntry, Button, or Label
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_GetUnicodeLen_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	uintptr_t vtable = *reinterpret_cast<uintptr_t*>(panel);

	// Determine which GetText(wchar) vtable slot to use based on panel type
	ptrdiff_t getTextOffset = 0;
	if (PanelVtableBool(panel, PANEL_VT_ISTEXTENTRY))
		getTextOffset = PANEL_VT_TEXTENTRY_GETTEXT_W;
	else if (PanelVtableBool(panel, PANEL_VT_ISBUTTON) || PanelVtableBool(panel, PANEL_VT_ISLABEL))
		getTextOffset = PANEL_VT_BUTTON_GETTEXT_W;

	if (!getTextOffset)
	{
		v_SQVM_ScriptError("Hud element is not a text panel. (%s)", GetPanelName(panel));
		sq_pushinteger(v, 0);
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
	}

	// Call GetText to get wchar buffer, then measure unicode length
	wchar_t textBuf[4096];
	textBuf[0] = L'\0';
	typedef void(__fastcall* GetTextFn)(uintptr_t, wchar_t*, int);
	reinterpret_cast<GetTextFn>(*reinterpret_cast<uintptr_t*>(vtable + getTextOffset))(
		panel, textBuf, 4096);

	sq_pushinteger(v, static_cast<SQInteger>(wcslen(textBuf)));
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_GetUnicodeLen(HSQUIRRELVM v) { return Script_Hud_GetUnicodeLen_Impl(v); }
static SQRESULT UIScript_Hud_GetUnicodeLen(HSQUIRRELVM v) { return Script_Hud_GetUnicodeLen_Impl(v); }

//-----------------------------------------------------------------------------
// Hud_InitGridButtonsCategories(var hudElement, array categories)
// Initializes grid button categories on a GridButtonListPanel
// This is a simplified stub since the full implementation requires deep
// GridButtonListPanel internal access (CUtlVector<short> m_Categories)
//-----------------------------------------------------------------------------
static SQRESULT Script_Hud_InitGridButtonsCategories_Impl(HSQUIRRELVM v)
{
	uintptr_t hudElem = GetHudElement(v);
	if (!hudElem)
	{
		v_SQVM_ScriptError("First parameter is not a hud element");
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	uintptr_t panel = GetPanel(hudElem);
	if (!PanelVtableBool(panel, PANEL_VT_ISGRIDBTNLIST))
	{
		v_SQVM_ScriptError("No GridButtonListPanel element with name '%s'.", GetPanelName(panel));
		SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
	}

	// Consume the array parameter (stack index 3) silently
	// The actual SetCategories call requires internal CUtlVector manipulation
	// which we'll add if needed based on runtime errors
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}
static SQRESULT ClientScript_Hud_InitGridButtonsCategories(HSQUIRRELVM v) { return Script_Hud_InitGridButtonsCategories_Impl(v); }
static SQRESULT UIScript_Hud_InitGridButtonsCategories(HSQUIRRELVM v) { return Script_Hud_InitGridButtonsCategories_Impl(v); }

//=============================================================================
// Registration
//=============================================================================

//-----------------------------------------------------------------------------
// Purpose: registers color palette + utility functions in CLIENT context
//-----------------------------------------------------------------------------
void Script_RegisterColorPaletteFunctions(CSquirrelVM* s)
{
	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_RGBtoHSV,
		"Converts RGB color (0-255) to HSV (H=0-360, S=0-1, V=0-1)",
		"vector", "vector rgbColor", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_HSVtoRGB,
		"Converts HSV color (H=0-360, S=0-1, V=0-1) to RGB (0-255)",
		"vector", "vector hsvColor", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_ClampAndValidateColor,
		"Clamps color brightness to minimum 50%",
		"vector", "vector rgbColor", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_ClampAndValidateLaserSightColor,
		"Clamps laser sight color brightness using ConVar min/max",
		"vector", "vector rgbColor", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_GetDefaultColorFromID,
		"Gets default (non-overridden) color from palette by ID",
		"vector", "int colorID", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_GetColorFromID,
		"Gets color from palette by ID (override if set, else default)",
		"vector", "int colorID", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_GetIDFromString,
		"Gets palette index from a color name, or -1 if not found",
		"int", "string colorName", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, ColorPalette_IsColorCustomized,
		"Returns true if the palette color at this ID has been overridden",
		"bool", "int colorID", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetConVarColor,
		"Gets a ConVar value as an RGB color vector (0-255)",
		"vector", "string cvarName", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, SetConVarColor,
		"Sets a ConVar value from an RGB color vector (0-255)",
		"void", "string cvarName, vector rgbColor", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_SliderControl_SetCurrentValue,
		"Sets slider control value for a HUD element",
		"void", "var hudElement, float value", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_SliderControl_GetCurrentValue,
		"Gets slider control value from a HUD element",
		"float", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, FormatAndLocalizeNumber,
		"Formats a number using RUI-style format string",
		"string", "string format, var number, bool addThousandsSeparator", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_GetButtonCount,
		"Returns the button count of a GridButtonListPanel",
		"int", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_GetButton,
		"Returns a button at the given index from a GridButtonListPanel",
		"var", "var hudElement, int index", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_GetSelectedButton,
		"Returns the selected button from a GridButtonListPanel",
		"var", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_SetChecked,
		"Sets the checked state on a button element",
		"void", "var hudElement, bool checked", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_IsChecked,
		"Returns whether a button element is checked",
		"bool", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_ClearEventHandlers,
		"Clears event handlers for the given event type",
		"void", "var hudElement, int eventType", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_GetTextHidden,
		"Returns whether text is hidden on a TextEntry",
		"bool", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_SetTextHidden,
		"Sets text hidden state on a TextEntry",
		"void", "var hudElement, bool hidden", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_ScrollToTop,
		"Scrolls a GridButtonListPanel to the top",
		"void", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_IsCursorOver,
		"Returns whether the cursor is over a HUD element",
		"bool", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_DialogList_ClearList,
		"Removes all items from a DialogListButton",
		"void", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_GetUnicodeLen,
		"Returns the unicode length of text in a text panel",
		"int", "var hudElement", false);

	DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, Hud_InitGridButtonsCategories,
		"Initializes grid button categories on a GridButtonListPanel",
		"void", "var hudElement, array categories", true);
}

//-----------------------------------------------------------------------------
// Purpose: registers color palette functions in UI context
//-----------------------------------------------------------------------------
void Script_RegisterColorPaletteUIFunctions(CSquirrelVM* s)
{
	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_RGBtoHSV,
		"Converts RGB color (0-255) to HSV (H=0-360, S=0-1, V=0-1)",
		"vector", "vector rgbColor", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_HSVtoRGB,
		"Converts HSV color (H=0-360, S=0-1, V=0-1) to RGB (0-255)",
		"vector", "vector hsvColor", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_ClampAndValidateColor,
		"Clamps color brightness to minimum 50%",
		"vector", "vector rgbColor", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_ClampAndValidateLaserSightColor,
		"Clamps laser sight color brightness using ConVar min/max",
		"vector", "vector rgbColor", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_GetDefaultColorFromID,
		"Gets default (non-overridden) color from palette by ID",
		"vector", "int colorID", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_GetColorFromID,
		"Gets color from palette by ID (override if set, else default)",
		"vector", "int colorID", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_GetIDFromString,
		"Gets palette index from a color name, or -1 if not found",
		"int", "string colorName", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, ColorPalette_IsColorCustomized,
		"Returns true if the palette color at this ID has been overridden",
		"bool", "int colorID", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, GetConVarColor,
		"Gets a ConVar value as an RGB color vector (0-255)",
		"vector", "string cvarName", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, SetConVarColor,
		"Sets a ConVar value from an RGB color vector (0-255)",
		"void", "string cvarName, vector rgbColor", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_SliderControl_SetCurrentValue,
		"Sets slider control value for a HUD element",
		"void", "var hudElement, float value", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_SliderControl_GetCurrentValue,
		"Gets slider control value from a HUD element",
		"float", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, OverrideColor,
		"Overrides a named color in the color palette",
		"void", "vector rgbColor, string colorName", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, FormatAndLocalizeNumber,
		"Formats a number using RUI-style format string",
		"string", "string format, var number, bool addThousandsSeparator", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_GetButtonCount,
		"Returns the button count of a GridButtonListPanel",
		"int", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_GetButton,
		"Returns a button at the given index from a GridButtonListPanel",
		"var", "var hudElement, int index", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_GetSelectedButton,
		"Returns the selected button from a GridButtonListPanel",
		"var", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_SetChecked,
		"Sets the checked state on a button element",
		"void", "var hudElement, bool checked", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_IsChecked,
		"Returns whether a button element is checked",
		"bool", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_ClearEventHandlers,
		"Clears event handlers for the given event type",
		"void", "var hudElement, int eventType", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_GetTextHidden,
		"Returns whether text is hidden on a TextEntry",
		"bool", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_SetTextHidden,
		"Sets text hidden state on a TextEntry",
		"void", "var hudElement, bool hidden", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_ScrollToTop,
		"Scrolls a GridButtonListPanel to the top",
		"void", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_IsCursorOver,
		"Returns whether the cursor is over a HUD element",
		"bool", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_DialogList_ClearList,
		"Removes all items from a DialogListButton",
		"void", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_GetUnicodeLen,
		"Returns the unicode length of text in a text panel",
		"int", "var hudElement", false);

	DEFINE_UI_SCRIPTFUNC_NAMED(s, Hud_InitGridButtonsCategories,
		"Initializes grid button categories on a GridButtonListPanel",
		"void", "var hudElement, array categories", true);
}

//-----------------------------------------------------------------------------
// Purpose: registers player class script functions (called from
//          Script_RegisterClientPlayerClassFuncs in vscript_client.cpp)
//-----------------------------------------------------------------------------
void Script_RegisterPlayerScriptFunctions(ScriptClassDescriptor_t* playerStruct)
{
	playerStruct->AddFunction(
		"IsConnectionActive",
		"Script_IsConnectionActive",
		"Returns true if the player's network connection is active",
		"bool",
		"",
		false,
		Script_IsConnectionActive);
}
