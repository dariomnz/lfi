
cmake_minimum_required(VERSION 3.16)

project(LFI VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# add_compile_options("-DDEBUG")
add_compile_options("-fPIC" "-O3" "-g" "-ggdb" "-gdwarf-4" "-Wall" "-Wextra")

link_libraries("pthread" "dl")

# add_compile_options(-fsanitize=thread)
# add_link_options(-fsanitize=thread)
# add_compile_options(-fsanitize=address)
# add_link_options(-fsanitize=address)
# add_compile_options(-fsanitize=undefined)
# add_link_options(-fsanitize=undefined)

add_subdirectory(src)
option(BUILD_LD_PRELOAD "Build the LD_PRELOAD library" OFF)
if(BUILD_LD_PRELOAD)
    message(STATUS "BUILD_LD_PRELOAD : true")
    add_subdirectory(ld_preload)
else()
    message(STATUS "BUILD_LD_PRELOAD : false")
endif(BUILD_LD_PRELOAD)
unset(BUILD_LD_PRELOAD CACHE)


option(BUILD_EXAMPLES "Build the examples" OFF)
if(BUILD_EXAMPLES)
    message(STATUS "BUILD_EXAMPLES : true")
    add_subdirectory(examples)
else()
    message(STATUS "BUILD_EXAMPLES : false")
endif(BUILD_EXAMPLES)
unset(BUILD_EXAMPLES CACHE)
