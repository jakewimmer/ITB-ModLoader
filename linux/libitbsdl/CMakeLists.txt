cmake_minimum_required(VERSION 3.16)
project(itbsdl CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(SDL2 REQUIRED)
find_package(Freetype REQUIRED)
find_package(Fontconfig REQUIRED)

# OpenGL: host has GLVND (libOpenGL.so.0) but no GL/gl.h dev files.
# We use bundled GLEW headers from the proxy and link to system OpenGL.
# Link directly to the GLVND library.
set(OPENGL_LIBRARIES /lib64/libOpenGL.so.0)

# Lua 5.1 is NOT vendored/linked. The game already runs one Lua 5.1 runtime; a
# second embedded copy sharing the game's lua_State corrupted the heap. We use
# only the vendored HEADERS (lua.hpp / lua.h on the include path below) to compile
# against the C API and leave every lua_* UNDEFINED, resolved at load time from
# libitbboot.so, which re-exports the game's Lua symbols. (No add_subdirectory of
# third_party/lua-5.1.5, no lua51_static link.)

# Sources grow as later tasks land: Task 2 adds interpose.cpp, Task 3 screen_gl.cpp,
# Task 4 text_freetype.cpp, Task 5 os_posix.cpp.
add_library(itbsdl SHARED
    src/itbsdl.cpp
    src/interpose.cpp
    src/screen_gl.cpp
    src/text_freetype.cpp
    src/os_posix.cpp
    third_party/glew/glew.c
)

# GLEW compilation requires -DGLEW_NO_GLU to avoid requiring GL/glu.h
# and -D__ANDROID__ to skip glxew.h which requires X11 headers
set_source_files_properties(third_party/glew/glew.c PROPERTIES
    COMPILE_FLAGS "-DGLEW_STATIC -DGLEW_NO_GLU -D__ANDROID__")

target_include_directories(itbsdl PRIVATE
    src ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua-5.1.5
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glew
    ${SDL2_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${Fontconfig_INCLUDE_DIRS})

# Apply GLEW defines to all source files
target_compile_definitions(itbsdl PRIVATE GLEW_STATIC GLEW_NO_GLU)

# SDL2 is only needed for headers; we don't link it (game provides its own at runtime).
# Task 1 calls no SDL functions. Task 2+ will interpose SDL functions via dlsym.
# OpenGL is linked because it's a system library; Task 3 uses GLVND.
# FreeType and Fontconfig are linked because they're required by text_freetype.cpp.
# Lua is intentionally absent: lua_* stays undefined, resolved from libitbboot.so.
target_link_libraries(itbsdl PRIVATE
    ${FREETYPE_LIBRARIES} ${Fontconfig_LIBRARIES} ${OPENGL_LIBRARIES} dl)

# Output as libitbsdl.so; staged to repo root by the packaging step.
set_target_properties(itbsdl PROPERTIES OUTPUT_NAME itbsdl PREFIX "lib")
