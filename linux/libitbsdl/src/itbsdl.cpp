// Shared module state. Because the same .so is loaded via LD_PRELOAD and
// package.loadlib, these globals are shared between the SDL interposers and the
// Lua-registered callbacks.
#include <lua.h>
#include <lualib.h>
#include <vector>

lua_State *g_lua = nullptr;        // captured at luaopen_itbsdl
std::vector<void *> g_draw_hooks;  // typed properly in Task 2
std::vector<void *> g_event_hooks;

extern "C" int luaopen_itbsdl(lua_State *L) {
    g_lua = L;
    // install_sdl_namespace(L); install_os_namespace(L);  // wired up in Tasks 2–5
    return 0;
}
