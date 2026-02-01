# AI usage disclosure

The work in this repo is done by a person who knows how to code and AI. The main reason for the AI usage is,
is the lack of time to learn C++ or Squirrel to fully understand things.

## Goal

The goal of the project is to have Bots in Apex Legends R5Reloaded, that can navigate a complex map,
loot and shoot at enemies. Using abilities based on the legends they are might be added depending on the complexity.

For the actual bots mod, see https://github.com/dilirity/r5_apex_bots

## Modifications made to allow the bots to be possible (will try to keep this updated)

https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots

- [Physics_RunBotSimulation](https://github.com/dilirity/r5sdk/blob/aea234647ec1265eb8d9a9002fb881eb0eb0d69c/src/game/server/physics_main.cpp#L22) had to be updated so it doesn't run [`RunNullCommand`](https://github.com/R5Reloaded/r5sdk/blob/a27ea9e20d564258c8a86591bde1404fa6d38f65/src/game/server/physics_main.cpp#L33).
- [The visibility of a couple of variables](https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots#diff-a1327d987ab46da298a7ea096de075495f242b0f27c659237a4709386e86bc20) needed to be changed so we can expose them to scripting (not sure if we need it though).
- [CPlayerMove::StaticRunCommand](https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots#diff-c3dce5e0293fa4bb70d077a45b6b916ce17909a4596843a35276f4096caa926f) had to be updated so we can send inputs from the bots (this needs validation at some point).
- [BotInput](https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots#diff-a4088623d426b43ccc5f7811813ff17e69295aa91b224da772fa0c54cfc30e40) added to aid the above.
- Lots of changes in [src/game/server/vscript_server.cpp](https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots#diff-c40f4588cd9163db29b5c8ecb5cecba6b49d82146a8dce519b87ed3151c334b1):
  - Internal_FindNearestPolyByHeight can probably be removed;
  - NavMesh API needed more exposure:
    - ServerScript_NavMesh_CreateCorridor
    - ServerScript_NavMesh_DestroyCorridor
    - ServerScript_NavMesh_CorridorSetPath
    - ServerScript_NavMesh_CorridorMove
    - ServerScript_NavMesh_CorridorGetCorners
    - ServerScript_NavMesh_GetTileTraversePortals
  - Have more control over bot creation:
    - ServerScript_Bot_Create
  - Have more control over what inputs the bots send:
    - ServerScript_SetBotInput
    - ServerScript_BotStopPersistentInput (this could probably be replaced by BotButtonPress/BotButtonRelease, not sure)
- console command `navmesh_draw_traverse_portals_type` was updated to support a list of comma separated types. This makes visual debugging easier, so you don't have to go through all types to see if any connect polygons you're debugging:
  - This will probably either be reverted or properly implemented. It's really helpful for debugging;
  - As a result, a couple of other files needed to be updated as the code is used there as well:
    - [src/naveditor/Editor.cpp](https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots#diff-141ffd937a1f8ab34bd73d1b42d21131c5f376821e30808da840ab291b192ad6);
    - [src/thirdparty/recast/DebugUtils/Include/DetourDebugDraw.h](https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots#diff-cbca3836ec1a8b5dbec30d56331e413ab2dda2ba6507bdf0698234dbed3e2201);
    - [src/thirdparty/recast/DebugUtils/Source/DetourDebugDraw.cpp](https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots#diff-a48a4a90a05906f9bc9d3fad4cfb1d480ab2a64c782fdb85a6aa68e026a94398);
- Fixed some failed assertions (this is probably me not knowing how to work with VS 2019 more than anything. there's probably a setting or something that needs to be adjusted):
  - [src/loader/loader.cpp](https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots#diff-f057b2d856c96e9cc025b7157fc8a677f1e5120f95a33438638f94fce816aac4);
  - [src/rtech/rui/rui.cpp](https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots#diff-1a2fdf064bdf25124d456661a83dd4aab996050ddec11d25feb9fbebce1ce026);
  - [src/tier1/kvleaktrace.h](https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots#diff-53e4da0f4639ba0596467305a31c242ecb94acef98f3c80cca0777eb83fea7cf);
- Needed more information about the polygon corners from [`dtNavMeshQuery::findStraightPath`](https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots#diff-204719d27f6eac2d173143d698d9634c16d8d74aaf77dd6e0eb9a47d8e5d34e3). This is because of the way I'm implementing navigation. A better way might not need this;
- More nav data:
  - [src/thirdparty/recast/DetourCrowd/Include/DetourPathCorridor.h](https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots#diff-38b07ea1d3d34f886516139d3c2b916bdc6dc4f5ad5cfe2e3d0a33ec8baca33b);
  - traverse types [src/thirdparty/recast/DetourCrowd/Source/DetourPathCorridor.cpp](https://github.com/R5Reloaded/r5sdk/compare/p4sync...dilirity:r5sdk:bots#diff-62209c824804e44630355145091e2ea74a1792506145c32539c83f215d5a336d);

Original readme below.

----

## Source SDK
* This repository houses the source code for the development package targeting the game **Apex Legends**.

## Building
R5sdk uses the CMake project generation and build tools. For more information, visit [CMake](https://cmake.org/).<br />
In order to compile the SDK, you will need to install Visual Studio 2017, 2019 or 2022 with:
* Desktop Development with C++ Package.
* Windows SDK 10.0.10240.0 or higher.
* C++ MFC build tools for x86 and x64.
* [Optional] C++ Clang/LLVM compiler.

Steps:
1. Download or clone the project to anywhere on your disk.
    1. Run `CreateSolution.bat` in the root folder, this will generate the files in `build_intermediate`.
    2. Move all the game files in the `game` folder so that the path `game/r5apex(_ds).exe` is valid.
2. Open `r5sdk.sln` in Visual Studio and compile the solution.
    1. All binaries and symbols are compiled to the `game` folder.
    2. Run `launcher.exe`, toggle and set the desired options and hit the `Launch Game` button.

## Steamworks Integration [OPTIONAL]
The SDK includes optional Steam integration for features like authentication, user profiles, and overlay support. **This is completely optional** - the SDK works without Steam.

### Setting up Steamworks (Optional)
Due to licensing restrictions, the Steamworks SDK is not included in this repository. If you want Steam features:

1. **Download the Steamworks SDK:**
   - Visit [Steamworks SDK](https://partner.steamgames.com/) (requires Steam partner account)
   - Download the latest Steamworks SDK

2. **Install the SDK:**
   - Extract the SDK to `src/thirdparty/steamworks/sdk/`
   - The folder structure should look like:
     ```
     src/thirdparty/steamworks/sdk/
     ├── public/steam/
     ├── redistributable_bin/
     └── Readme.txt
     ```

3. **Enable Steam features:**
   - Add `#define USE_STEAMWORKS` to your project or compiler flags
   - The build system will automatically link against Steam libraries when available

### Steam Features
When Steamworks is enabled, the SDK provides:
- **Steam Authentication**: Session tickets for server authentication
- **User Profiles**: Access to Steam username and user ID
- **Steam Overlay**: Integration with Steam's in-game overlay
- **Safe Integration**: Automatic fallbacks when Steam is unavailable

### Console Commands (with Steam)
- `steam_overlay_info` - Display Steam overlay status and settings
- `steam_overlay_pos <0-3>` - Set overlay notification position
- `steam_safe_callbacks <0/1>` - Enable/disable safe callback processing

### Building without Steam
The SDK compiles and runs perfectly without Steamworks. All Steam-related code is conditionally compiled and safely disabled when the SDK is not available.

## Debugging
The tools and libraries offered by the SDK could be debugged right after they are compiled.

Steps:
1. Set the target project as **Startup Project**.
    1. Select `Project -> Set as Startup Project`.
2. Configure the project's debugging settings.
    1. Debug settings are found in `Project -> Properties -> Configuration Properties -> Debugging`.
    2. Additional command line arguments could be set in the `Command Arguments` field.

## Launch Parameters
- The `-wconsole` parameter toggles the external console window to which output of the game is getting logged to.
- The `-ansicolor` parameter enables colored console output to enhance readability (NOTE: unsupported for some OS versions!).
- The `-nosmap` parameter instructs the SDK to always compute the RVA's of each function signature on launch (!! slow !!).
- The `-noworkerdll` parameter prevents the GameSDK DLL from initializing (workaround as the DLL is imported by the game executable).

Launch parameters can be added to the `startup_*.cfg` files,<br />
which are located in `<gamedir>\platform\cfg\startup_*.cfg`.

## Note [IMPORTANT]
This is not a cheat or hack; attempting to use the SDK on the live version of the game could result in a permanent account ban. The supported game versions are:

 * S3 `R5pc_r5launch_N1094_CL456479_2019_10_30_05_20_PM`.

## Pylon [DISCLAIMER]
When you host game servers on the Server Browser (Pylon) you will stream your IP address to the database,
which will be stored there until you stop hosting the server; this is needed so other people can connect to your server.

There is a checkbox in the Server Browser called `Server Visibility` that defaults to `Offline`.
- `Offline`: No data is broadcasted to the master server; you are playing offline.
- `Hidden`: Your server will be broadcasted to the master server, but could only be joined using a private token.
- `Online`: Your server will be broadcasted to the master server, and could be joined from the public list.

Alternatively, you can host game servers without the use of our master server. You can grant people access to your game server
by sharing the IP address and port manually. The client can connect using the `connect` command. The usage of the `connect`
command is as follows: IPv4 `connect 127.0.0.1:37015`, IPv6 `connect [::1]:37015`. NOTE: the IP address and port were examples.
