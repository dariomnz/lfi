
file(GLOB_RECURSE LFI_HEADERS
	"*.hpp"
)

file(GLOB_RECURSE LFI_SOURCE
	"*.cpp"
)

# Libfabric include
find_library(LIBFABRIC_LIBRARY NAMES fabric PATHS ${LIBFABRIC_PATH}/lib /usr/lib /usr/local/lib)
find_path(LIBFABRIC_INCLUDE_DIR rdma/fabric.h PATHS ${LIBFABRIC_PATH}/include /usr/include /usr/local/include)

add_library(lfi SHARED ${LFI_HEADERS} ${LFI_SOURCE})

add_library(lfi_static STATIC $<TARGET_OBJECTS:lfi>)
set_target_properties(lfi_static PROPERTIES OUTPUT_NAME lfi)

target_include_directories(lfi PUBLIC
	"${PROJECT_SOURCE_DIR}/include"
	"${PROJECT_SOURCE_DIR}/include/impl"
)

target_include_directories(lfi_static PUBLIC
	"${PROJECT_SOURCE_DIR}/include"
	"${PROJECT_SOURCE_DIR}/include/impl"
)

if(LIBFABRIC_LIBRARY AND LIBFABRIC_INCLUDE_DIR)
    target_include_directories(lfi PUBLIC ${LIBFABRIC_INCLUDE_DIR})
    target_include_directories(lfi_static PUBLIC ${LIBFABRIC_INCLUDE_DIR})
    target_link_libraries(lfi PUBLIC ${LIBFABRIC_LIBRARY})
    target_link_libraries(lfi_static PUBLIC ${LIBFABRIC_LIBRARY})
    message(STATUS "LIBFABRIC_LIBRARY: ${LIBFABRIC_LIBRARY}")
    message(STATUS "LIBFABRIC_INCLUDE_DIR: ${LIBFABRIC_INCLUDE_DIR}")
else()
    message(FATAL_ERROR "libfabric not found.  Make sure it is installed on your system. Or the LIBFABRIC_PATH is set correctly")
endif()

install(TARGETS lfi LIBRARY DESTINATION lib)
install(TARGETS lfi_static LIBRARY DESTINATION lib)
install(FILES "${PROJECT_SOURCE_DIR}/include/lfi.h" DESTINATION include)
install(FILES "${PROJECT_SOURCE_DIR}/include/lfi_async.h" DESTINATION include)
install(FILES "${PROJECT_SOURCE_DIR}/include/lfi_coll.h" DESTINATION include)
install(FILES "${PROJECT_SOURCE_DIR}/include/lfi_error.h" DESTINATION include)