cmake_minimum_required(VERSION 3.16)

project(discordsdk)

# Discord Game SDK library
add_library(discordsdk STATIC
    cpp/core.cpp
    cpp/types.cpp
    cpp/activity_manager.cpp
    cpp/user_manager.cpp
    cpp/achievement_manager.cpp
    cpp/application_manager.cpp
    cpp/image_manager.cpp
    cpp/lobby_manager.cpp
    cpp/network_manager.cpp
    cpp/overlay_manager.cpp
    cpp/relationship_manager.cpp
    cpp/storage_manager.cpp
    cpp/store_manager.cpp
    cpp/voice_manager.cpp
)

# Set folder for Visual Studio organization
if(FOLDER_CONTEXT)
    set_target_properties(discordsdk PROPERTIES FOLDER "${FOLDER_CONTEXT}")
endif()

target_include_directories(discordsdk PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp
)

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit
        target_link_libraries(discordsdk PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lis/x86_64/discord_game_sdk.dll.lib")
        
        # Define paths
        set(DISCORD_DLL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libs/x86_64/discord_game_sdk.dll")
        set(STEAM_DLL_PATH "${CMAKE_SOURCE_DIR}/src/thirdparty/steamworks/sdk/redistributable_bin/win64/steam_api64.dll")
        set(GAME_DIR "${CMAKE_BINARY_DIR}/../game")
        
        # Copy DLLs and create steam_appid.txt
        add_custom_command(TARGET discordsdk POST_BUILD
            # Copy Discord Game SDK DLL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DISCORD_DLL_PATH}"
            "${GAME_DIR}/"
            
            # Copy Steam API DLL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${STEAM_DLL_PATH}"
            "${GAME_DIR}/"
            
            # Create steam_appid.txt with Apex App ID
            COMMAND cmd /c "echo 1172470 > \"${GAME_DIR}/steam_appid.txt\""
            
            COMMENT "Copying Discord and Steam DLLs, creating steam_appid.txt"
        )
    else()
        # 32-bit
        target_link_libraries(discordsdk PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lib/x86/discord_game_sdk.dll.lib")
        
        # Define paths for 32-bit
        set(DISCORD_DLL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lib/x86/discord_game_sdk.dll")
        set(STEAM_DLL_PATH "${CMAKE_SOURCE_DIR}/src/thirdparty/steamworks/sdk/redistributable_bin/steam_api.dll")
        set(GAME_DIR "${CMAKE_BINARY_DIR}/../game")
        
        # Copy DLLs and create steam_appid.txt
        add_custom_command(TARGET discordsdk POST_BUILD
            # Copy Discord Game SDK DLL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DISCORD_DLL_PATH}"
            "${GAME_DIR}/"
            
            # Copy Steam API DLL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${STEAM_DLL_PATH}"
            "${GAME_DIR}/"
            
            # Create steam_appid.txt with Apex App ID
            COMMAND cmd /c "echo 1172470 > \"${GAME_DIR}/steam_appid.txt\""
            
            COMMENT "Copying Discord and Steam DLLs, creating steam_appid.txt"
        )
    endif()
endif()

set_target_properties(discordsdk PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
