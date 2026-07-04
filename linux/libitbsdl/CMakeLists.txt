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
    src/gl_interpose.cpp
    src/screen_gl.cpp
    src/text_freetype.cpp
    src/os_posix.cpp
    third_party/glew/glew.c
)

# GLEW compilation requires -D__ANDROID__ to skip glxew.h which requires X11 headers
set_source_files_properties(third_party/glew/glew.c PROPERTIES
    COMPILE_FLAGS "-D__ANDROID__")

target_include_directories(itbsdl PRIVATE
    src)

# Third-party headers are included with -isystem to suppress warnings from our src/
target_include_directories(itbsdl SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua-5.1.5
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glew
    ${SDL2_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${Fontconfig_INCLUDE_DIRS})

# Apply GLEW defines to all source files (unified in target_compile_definitions)
target_compile_definitions(itbsdl PRIVATE GLEW_STATIC GLEW_NO_GLU)

# Enable strict warnings for src/ (not third_party/)
target_compile_options(itbsdl PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra>)

# SDL2 is only needed for headers; we don't link it (game provides its own at runtime).
# Task 1 calls no SDL functions. Task 2+ will interpose SDL functions via dlsym.
# OpenGL is linked because it's a system library; Task 3 uses GLVND.
# FreeType and Fontconfig are linked because they're required by text_freetype.cpp.
# Lua is intentionally absent: lua_* stays undefined, resolved from libitbboot.so.
target_link_libraries(itbsdl PRIVATE
    ${FREETYPE_LIBRARIES} ${Fontconfig_LIBRARIES} ${OPENGL_LIBRARIES} dl)

# Output as libitbsdl.so; staged to repo root by the packaging step.
set_target_properties(itbsdl PROPERTIES OUTPUT_NAME itbsdl PREFIX "lib")

# Test for alloc_fits helper
enable_testing()
add_executable(alloc_guard_test test/alloc_guard_test.cpp)
target_include_directories(alloc_guard_test PRIVATE src)
add_test(NAME alloc_guard_test COMMAND alloc_guard_test)

# Test that setBitmap stores/hashes pixels in RGBA order (no channel swap), the
# contract wasDrawn()/onModsLoaded depends on. Links libitbsdl for setBitmap and
# simple_hash; those paths call no game-provided (Lua/SDL/GL) symbols, so lazy
# binding lets the .so's undefined symbols stay unresolved at load.
add_executable(setbitmap_rgba_test test/setbitmap_rgba_test.cpp)
target_include_directories(setbitmap_rgba_test PRIVATE src)
target_include_directories(setbitmap_rgba_test SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(setbitmap_rgba_test PRIVATE itbsdl)
target_link_options(setbitmap_rgba_test PRIVATE
    -Wl,-z,lazy -Wl,--unresolved-symbols=ignore-all)
add_test(NAME setbitmap_rgba_test COMMAND setbitmap_rgba_test)
