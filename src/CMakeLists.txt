
file(GLOB_RECURSE LFI_HEADERS
	"*.hpp"
)

file(GLOB_RECURSE LFI_SOURCE
	"*.cpp"
)

# Libfabric include
find_library(LIBFABRIC_LIBRARY NAMES fabric PATHS /usr/lib /usr/local/lib ${LIBFABRIC_PATH}/lib)
find_path(LIBFABRIC_INCLUDE_DIR rdma/fabric.h PATHS /usr/include /usr/local/include ${LIBFABRIC_PATH}/include)

add_library(lfi STATIC ${LFI_HEADERS} ${LFI_SOURCE})

add_library(lfi_shared SHARED $<TARGET_OBJECTS:lfi>)
set_target_properties(lfi_shared PROPERTIES OUTPUT_NAME lfi)

target_include_directories(lfi PUBLIC
	"${PROJECT_SOURCE_DIR}/include"
	"${PROJECT_SOURCE_DIR}/include/impl"
)

if(LIBFABRIC_LIBRARY AND LIBFABRIC_INCLUDE_DIR)
    target_include_directories(lfi PUBLIC ${LIBFABRIC_INCLUDE_DIR})
    target_include_directories(lfi_shared PUBLIC ${LIBFABRIC_INCLUDE_DIR})
    target_link_libraries(lfi PUBLIC ${LIBFABRIC_LIBRARY})
    target_link_libraries(lfi_shared PUBLIC ${LIBFABRIC_LIBRARY})
else()
    message(FATAL_ERROR "libfabric not found.  Make sure it is installed on your system. Or the LIBFABRIC_PATH is set correctly")
endif()

install(TARGETS lfi LIBRARY DESTINATION lib)
install(TARGETS lfi_shared LIBRARY DESTINATION lib)
install(FILES "${PROJECT_SOURCE_DIR}/include/lfi.h" DESTINATION include)