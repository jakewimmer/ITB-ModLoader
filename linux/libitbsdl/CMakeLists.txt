cmake_minimum_required(VERSION 3.16)
project(itbsdl CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(SDL2 REQUIRED)
# OpenGL deferred to Task 3 (host has no GL/gl.h; we'll use GLEW headers from proxy)
# find_package(OpenGL REQUIRED)
find_package(Freetype REQUIRED)

add_subdirectory(third_party/lua-5.1.5)

# Sources grow as later tasks land: Task 2 adds interpose.cpp, Task 3 screen_gl.cpp,
# Task 4 text_freetype.cpp, Task 5 os_posix.cpp. Task 1 lists only itbsdl.cpp.
add_library(itbsdl SHARED
    src/itbsdl.cpp
)

target_include_directories(itbsdl PRIVATE
    src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LuaBridge
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua-5.1.5
    ${SDL2_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})

# SDL2 is only needed for headers; we don't link it (game provides its own at runtime).
# Task 1 calls no SDL functions. Task 2+ will interpose SDL functions via dlsym.
target_link_libraries(itbsdl PRIVATE
    lua51_static ${FREETYPE_LIBRARIES} dl)
# OpenGL::GL deferred to Task 3

# Output as libitbsdl.so; staged to repo root by the packaging step.
set_target_properties(itbsdl PROPERTIES OUTPUT_NAME itbsdl PREFIX "lib")
