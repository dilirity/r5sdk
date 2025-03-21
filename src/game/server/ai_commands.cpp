//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Console commands for debugging and manipulating NPCs.
//
//===========================================================================//
#include "tier1/convar.h"
#include "gameinterface.h"
#include "util_server.h"
#include "baseentity.h"

//------------------------------------------------------------------------------
// Purpose: Show the selected NPC's assault volume
//------------------------------------------------------------------------------
static void CC_NPC_AssaultVolume(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_ASSAULT_VOLUME);
}
static ConCommand npc_assault_volume("npc_assault_volume", CC_NPC_AssaultVolume, "Shows the volume representing the given NPC(s)'s assault point\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Select an NPC
//------------------------------------------------------------------------------
static void CC_NPC_Select(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_SELECTED_BIT);
}
static ConCommand npc_select("npc_select", CC_NPC_Select, "Select or deselects the given NPC(s) for later manipulation.  Selected NPC's are shown surrounded by a red translucent box\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Show the selected NPC's nearest and squad assigned node
//------------------------------------------------------------------------------
static void CC_NPC_Node(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_NODE_BIT);
}
static ConCommand npc_node("npc_node", CC_NPC_Node, "Draws a box around the given NPC(s)'s nodes in the following manner:\n\t	- nearest node (white)\n\t	- squad assigned node (blue)\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Show the selected NPC's route
//------------------------------------------------------------------------------
static void CC_NPC_Route(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_ROUTE_BIT);
}
static ConCommand npc_route("npc_route", CC_NPC_Route, "Displays the current route of the given NPC(s) as a line on the screen.  Way points along the route are drawn as small cyan rectangles.  Line is color coded in the following manner:\n\tBlue	- path to a node\n\tCyan	- detour around an object (triangulation)\n\tMaroon	- path to final target position\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Show route triangulation attempts
//------------------------------------------------------------------------------
static void CC_NPC_Bipass(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_TRIANGULATE_BIT);
}
static ConCommand npc_bipass("npc_bipass", CC_NPC_Bipass, "Displays the local movement attempts by the given NPC(s) (triangulation detours).  Failed bypass routes are displayed in red, successful bypasses are shown in green.\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Destroy selected NPC
//------------------------------------------------------------------------------
static void CC_NPC_Destroy(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_ZAP_BIT);
}
static ConCommand npc_destroy("npc_destroy", CC_NPC_Destroy, "Removes the given NPC(s) from the universe\nArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Show selected NPC's enemies
//------------------------------------------------------------------------------
static void CC_NPC_Enemies(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_ENEMIES_BIT);
}
static ConCommand npc_enemies("npc_enemies", CC_NPC_Enemies, "Shows memory of the given NPC(s).  Draws an X on top of each memory.\n\tEluded entities drawn in blue (don't know where it went)\n\tUnreachable entities drawn in green (can't get to it)\n\tCurrent enemy drawn in red\n\tCurrent target entity drawn in magenta\n\tAll other entities drawn in pink\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Shows all current conditions for an NPC.
//------------------------------------------------------------------------------
static void CC_NPC_Conditions(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_CONDITIONS_BIT);
}
static ConCommand npc_conditions("npc_conditions", CC_NPC_Conditions, "Displays all the current AI conditions that the given NPC(s) has in the overlay text.\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Show squad related data for an NPC
//------------------------------------------------------------------------------
static void CC_NPC_Squad(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_SQUAD_BIT);
}
static ConCommand npc_squads("npc_squads", CC_NPC_Squad, "Displays text debugging information about the squad and enemy of the selected NPC(s) (See Overlay Text)\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Show tasks for an NPC
//------------------------------------------------------------------------------
static void CC_NPC_Tasks(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_TASK_BIT);
}
static ConCommand npc_tasks("npc_tasks", CC_NPC_Tasks, "Displays detailed text debugging information about the all the tasks of the selected NPC(s)'s current schedule (See Overlay Text)\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Show selected NPC's current enemy and target entity
//------------------------------------------------------------------------------
static void CC_NPC_Focus(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_FOCUS_BIT);
}
static ConCommand npc_focus("npc_focus", CC_NPC_Focus, "Displays red line to NPC's enemy (if has one) and blue line to the given NPC(s)'s target entity (if has one)\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Show an NPC's sight volume
//------------------------------------------------------------------------------
static void CC_NPC_Sight(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_SIGHT_BIT);
}
static ConCommand npc_sight("npc_sight", CC_NPC_Sight, "Displays the sight volume of the given NPC(s) (where they are currently looking and what the extents of there vision is)\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: kill selected NPC
//------------------------------------------------------------------------------
static void CC_NPC_Kill(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_KILL_BIT);
}
static ConCommand npc_kill("npc_kill", CC_NPC_Kill, "Kills the given NPC(s)\nArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: sets npc to buddha mode
//------------------------------------------------------------------------------
static void CC_NPC_Buddha(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_BUDDHA_MODE);
}
static ConCommand npc_buddha("npc_buddha", CC_NPC_Buddha, "Set given NPC(s) to buddha mode\nArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Show an NPC's steering regulations
//------------------------------------------------------------------------------
static void CC_NPC_ViewSteeringRegulations(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_STEERING_REGULATIONS);
}
static ConCommand npc_steering("npc_steering", CC_NPC_ViewSteeringRegulations, "Displays the steering obstructions of the given NPC(s) (used to perform local avoidance)\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Show tasks (on the console) for an NPC
//------------------------------------------------------------------------------
static void CC_NPC_Task_Text(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_TASK_TEXT_BIT);
}
static ConCommand npc_task_text("npc_task_text", CC_NPC_Task_Text, "Outputs text debugging information to the console about the all the tasks + break conditions of the selected NPC(s)'s current schedule\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

//------------------------------------------------------------------------------
// Purpose: Show an NPC's relationships to other NPCs
//------------------------------------------------------------------------------
static void CC_NPC_Relationships(const CCommand& args)
{
	v_UTIL_SetDebugBits(UTIL_GetCommandClient(), args[1], OVERLAY_NPC_RELATION_BIT);
}
static ConCommand npc_relationships("npc_relationships", CC_NPC_Relationships, "Displays the relationships between the given NPC(s) and all others.\n\tArguments:   	{entity_name} / {class_name} / no argument picks what player is looking at", FCVAR_GAMEDLL | FCVAR_CHEAT);

// This is needed as Visual Studio seems to optimize the entire translation unit
// away as this file only implements console commands, but doesn't or isn't used
// from anything outside. It even gets optimized away when using the volatile
// keyword. This dummy is called from the file 'ai_utility.cpp' to ensure everything
// in this file makes it into the resulting library.
void AICommands_Dummy()
{
}
