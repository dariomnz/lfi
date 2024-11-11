
file(GLOB_RECURSE LFI_HEADERS
	"*.hpp"
)

file(GLOB_RECURSE LFI_SOURCE
	"*.cpp"
)

add_library(lfi STATIC ${LFI_HEADERS} ${LFI_SOURCE})

add_library(lfi_shared SHARED $<TARGET_OBJECTS:lfi>)
set_target_properties(lfi_shared PROPERTIES OUTPUT_NAME lfi)

target_include_directories(lfi PUBLIC
	"${PROJECT_SOURCE_DIR}/include"
	"${PROJECT_SOURCE_DIR}/include/impl"
)


install(TARGETS lfi LIBRARY DESTINATION lib)
install(TARGETS lfi_shared LIBRARY DESTINATION lib)
install(FILES "${PROJECT_SOURCE_DIR}/include/lfi.h" DESTINATION include)