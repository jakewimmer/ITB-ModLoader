cmake_minimum_required(VERSION 3.16)
project(lua51 C)

# Core Lua sources (excluding lua.c and luac.c)
add_library(lua51_static STATIC
    lapi.c
    lauxlib.c
    lbaselib.c
    lcode.c
    ldblib.c
    ldebug.c
    ldo.c
    ldump.c
    lfunc.c
    lgc.c
    linit.c
    liolib.c
    llex.c
    lmathlib.c
    lmem.c
    loadlib.c
    lobject.c
    lopcodes.c
    loslib.c
    lparser.c
    lstate.c
    lstring.c
    lstrlib.c
    ltable.c
    ltablib.c
    ltm.c
    lundump.c
    lvm.c
    lzio.c
)

target_include_directories(lua51_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(lua51_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
