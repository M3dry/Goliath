cmake_minimum_required(VERSION 3.21)
project("Goliath" VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_LINKER lld)

set(COMMON_COMPILE_OPTIONS
    -Wall -Wextra -Wuninitialized -Wmissing-field-initializers
    -Wno-unused-variable -Wshadow -Wconversion -Wsign-conversion
    -Wdouble-promotion -Wnull-dereference -Woverloaded-virtual -Wnon-virtual-dtor
    -O3 -ffunction-sections -fdata-sections
)
set(COMMON_LINK_OPTIONS
    -O3 -Wl,--gc-sections -s
)

add_subdirectory(src)
add_subdirectory(editor)
add_subdirectory(goiview)
#add_subdirectory(julip)
#add_subdirectory(ecs)