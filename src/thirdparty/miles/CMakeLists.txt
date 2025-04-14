cmake_minimum_required( VERSION 3.16 )
add_module( "lib" "milessdk" "" ${FOLDER_CONTEXT} TRUE TRUE )

start_sources()

add_sources( SOURCE_GROUP "Source/Shared/SDK"
    "src/sdk/shared/io.cpp"
    "src/sdk/shared/rrthreads2.h"
)

add_sources( SOURCE_GROUP "Include"
    "include/mss.h"
)

target_include_directories( ${PROJECT_NAME} PRIVATE
	"${THIRDPARTY_SOURCE_DIR}/miles/include/"
)

end_sources()
