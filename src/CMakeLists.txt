cmake_minimum_required( VERSION 3.16 )

set( FOLDER_CONTEXT "Foundation" )
add_subdirectory( vpc ) # VPC and Tier0 must be the first as this creates the shared PCH!
add_subdirectory( tier0 )
add_subdirectory( tier1 )
add_subdirectory( tier2 )
add_subdirectory( launcher )
add_subdirectory( appframework  )

set( FOLDER_CONTEXT "Libraries" )
add_subdirectory( mathlib )
add_subdirectory( vpklib )
add_subdirectory( vstdlib )
add_subdirectory( ebisusdk )
add_subdirectory( codecs )
add_subdirectory( geforce )
add_subdirectory( radeon )

set( FOLDER_CONTEXT "Protocols" )
add_subdirectory( protoc )

set( FOLDER_CONTEXT "Respawn" )
add_subdirectory( rtech )

set( FOLDER_CONTEXT "Thirdparty/Debugging" )
add_subdirectory( thirdparty/detours )
add_subdirectory( thirdparty/spdlog )

set( FOLDER_CONTEXT "Thirdparty/Navigation" )
add_subdirectory( thirdparty/recast )

set( FOLDER_CONTEXT "Thirdparty/Compression" )
add_subdirectory( thirdparty/lzham )
add_subdirectory( thirdparty/fastlz )
add_subdirectory( thirdparty/zstd )
add_subdirectory( thirdparty/lz4 )

set( FOLDER_CONTEXT "Thirdparty/Security" )
add_subdirectory( thirdparty/mbedtls )
add_subdirectory( thirdparty/jwt )

set( FOLDER_CONTEXT "Thirdparty/Multimedia" )
add_subdirectory( thirdparty/sdl )
add_subdirectory( thirdparty/imgui )
add_subdirectory( thirdparty/cppnet )
add_subdirectory( thirdparty/miles )
add_subdirectory( thirdparty/discordsdk )

set( FOLDER_CONTEXT "Thirdparty/Networking" )
add_subdirectory( thirdparty/protobuf )
add_subdirectory( thirdparty/curl )
add_subdirectory( thirdparty/dirtysdk )

set( FOLDER_CONTEXT "Thirdparty/Threading" )
add_subdirectory( thirdparty/ea/EAThread )

set( FOLDER_CONTEXT "Thirdparty/Boost" )
add_vendored_subdirectory( thirdparty/boost/libs/static_assert )
add_vendored_subdirectory( thirdparty/boost/libs/assert )
add_vendored_subdirectory( thirdparty/boost/libs/throw_exception )
add_vendored_subdirectory( thirdparty/boost/libs/core )
add_vendored_subdirectory( thirdparty/boost/libs/config )
add_vendored_subdirectory( thirdparty/boost/libs/charconv )
add_vendored_subdirectory( thirdparty/boost/libs/predef )
add_vendored_subdirectory( thirdparty/boost/libs/regex )

set( FOLDER_CONTEXT "Tools" )
add_subdirectory( sdklauncher )
add_subdirectory( netconsole )
add_subdirectory( naveditor )
add_subdirectory( revpk )

set( FOLDER_CONTEXT "System" )
add_subdirectory( networksystem )
add_subdirectory( pluginsystem )
add_subdirectory( materialsystem )
add_subdirectory( inputsystem )
add_subdirectory( filesystem )
add_subdirectory( datacache )
add_subdirectory( studiorender )
add_subdirectory( particles )
add_subdirectory( localize )
add_subdirectory( vphysics )
add_subdirectory( engine )
add_subdirectory( vguimatsurface )
add_subdirectory( vgui )
add_subdirectory( loader )

set( FOLDER_CONTEXT "Plugins" )
add_subdirectory( pluginsdk )
add_subdirectory( uisdk )

set( FOLDER_CONTEXT "Game" )
add_subdirectory( vscript )
add_subdirectory( game )

set( FOLDER_CONTEXT "/" )
add_subdirectory( core )

# Create a deployment target for copying runtime files
if(WIN32)
    # Define game directory path
    set(GAME_DEPLOY_DIR "${CMAKE_BINARY_DIR}/../game")
    
    # Create deployment target
    add_custom_target(deploy_runtime_files ALL
        DEPENDS discordsdk
        COMMENT "Deploying runtime files to game directory"
    )
    
    # Ensure game directory exists
    add_custom_command(TARGET deploy_runtime_files PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GAME_DEPLOY_DIR}"
    )
    
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit deployment
        add_custom_command(TARGET deploy_runtime_files POST_BUILD
            # Copy Discord Game SDK DLL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/thirdparty/discordsdk/lib/x86_64/discord_game_sdk.dll"
            "${GAME_DEPLOY_DIR}/"
            
            # Copy Steam API DLL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/thirdparty/steamworks/sdk/redistributable_bin/win64/steam_api64.dll"
            "${GAME_DEPLOY_DIR}/"
            
            # Create/update steam_appid.txt
            COMMAND ${CMAKE_COMMAND} -E echo "1172470" > "${GAME_DEPLOY_DIR}/steam_appid.txt"
            
            COMMENT "Deployed Discord SDK, Steam API, and steam_appid.txt (64-bit)"
        )
    else()
        # 32-bit deployment
        add_custom_command(TARGET deploy_runtime_files POST_BUILD
            # Copy Discord Game SDK DLL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/thirdparty/discordsdk/lib/x86/discord_game_sdk.dll"
            "${GAME_DEPLOY_DIR}/"
            
            # Copy Steam API DLL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/thirdparty/steamworks/sdk/redistributable_bin/steam_api.dll"
            "${GAME_DEPLOY_DIR}/"
            
            # Create/update steam_appid.txt
            COMMAND ${CMAKE_COMMAND} -E echo "1172470" > "${GAME_DEPLOY_DIR}/steam_appid.txt"
            
            COMMENT "Deployed Discord SDK, Steam API, and steam_appid.txt (32-bit)"
        )
    endif()
endif()
