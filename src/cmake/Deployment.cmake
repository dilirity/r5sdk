# Runtime file deployment for Discord SDK and Steam API
# This handles copying necessary DLLs and creating configuration files

if(WIN32)
    # Define game directory path
    set(GAME_DEPLOY_DIR "${CMAKE_BINARY_DIR}/../game")
    
    # Create deployment target
    add_custom_target(deploy_runtime_files ALL
        DEPENDS discordsdk
        COMMENT "Deploying runtime files to game directory"
    )
    
    # Set folder for Visual Studio organization
    set_target_properties(deploy_runtime_files PROPERTIES FOLDER "Build/Deployment")
    
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
