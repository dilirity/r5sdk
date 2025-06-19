cmake_minimum_required( VERSION 3.16 )
add_module( "shared_lib" "ui_sdk" "vpc" ${FOLDER_CONTEXT} TRUE TRUE )

start_sources()

add_sources( SOURCE_GROUP "Private"
    "ui_sdk.cpp"
)

end_sources( "${BUILD_OUTPUT_DIR}/paks/Win64/" )

target_link_libraries( ${PROJECT_NAME} PRIVATE
    "advapi32.lib"
    "bcrypt.lib"
    "crypt32.lib"
    "dbghelp.lib"
    "wldap32.lib"
    "ws2_32.lib"
    "Rpcrt4.lib"

    "vpc"
    "tier0"
    "tier1"

    "libdetours"
)

target_link_options( ${PROJECT_NAME} PRIVATE
    "/STACK:8000000" # Match game executable stack reserve size
)
