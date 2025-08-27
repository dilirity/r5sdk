cmake_minimum_required( VERSION 3.16 )
add_module( "lib" "codecs" "vpc" ${FOLDER_CONTEXT} TRUE TRUE )

start_sources()

add_sources( SOURCE_GROUP "Bink"
    "bink/bink_impl.cpp"
    "bink/bink_impl.h"
)

add_sources( SOURCE_GROUP "Miles"
    "miles/miles_impl.cpp"
    "miles/miles_impl.h"
    "miles/miles_shim.cpp"
    "miles/miles_shim.h"
    "miles/miles_types.h" # TODO[ AMOS ]: move to public!
    "miles/radshal_wasapi.h"
)

add_sources( SOURCE_GROUP "FMod"
    "fmod/audio_backend.h"
    "fmod/studio_backend.cpp"
    "fmod/audio_backend_state.cpp"
)

add_sources( SOURCE_GROUP "Public"
    "${ENGINE_SOURCE_DIR}/public/avi/iavi.h"
    "${ENGINE_SOURCE_DIR}/public/avi/ibik.h"
)

end_sources()

target_include_directories( ${PROJECT_NAME} PRIVATE "${ENGINE_SOURCE_DIR}/tier0/" "${ENGINE_SOURCE_DIR}/tier1/" )
target_link_libraries( ${PROJECT_NAME} PRIVATE "winmm.lib" )

target_include_directories( ${PROJECT_NAME} PRIVATE "${ENGINE_SOURCE_DIR}/thirdparty/fmod/inc" )

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(FMOD_ARCH_DIR "x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(FMOD_ARCH_DIR "x86")
else()
    set(FMOD_ARCH_DIR "x64")
endif()

set(FMOD_LIB_DIR "${ENGINE_SOURCE_DIR}/thirdparty/fmod/libs/${FMOD_ARCH_DIR}")
find_library(FMOD_LIB NAMES fmod_vc PATHS ${FMOD_LIB_DIR} NO_DEFAULT_PATH)
if (FMOD_LIB)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${FMOD_LIB})
endif()

# Try to link FMOD Studio if present
find_library(FMOD_STUDIO_LIB NAMES fmodstudio_vc PATHS ${FMOD_LIB_DIR} NO_DEFAULT_PATH)
if (FMOD_STUDIO_LIB)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${FMOD_STUDIO_LIB})
endif()