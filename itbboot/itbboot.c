/* libitbboot.so - LD_PRELOAD bootstrap injecting dlopen-based package.loadlib
 *
 * The native Linux Breach binary has Lua 5.1 compiled without LUA_DL_DLOPEN,
 * so package.loadlib is stubbed out. This bootstrap preload:
 * 1. Intercepts fopen/fopen64 when scripts/ are opened
 * 2. Searches for the main lua_State in game binary's BSS/memory
 * 3. Injects a dlopen(3)+dlsym(3)-based package.loadlib into the Lua VM
 * 4. Keeps an SDL_GL_SwapWindow hook for phase-3 proofs (frame hook + L reachable)
 *
 * The key challenge: the game calls luaL_newstate directly (not via PLT),
 * so we can't intercept it with LD_PRELOAD. Instead, we hook fopen which
 * definitely goes through PLT, and at that point search for the lua_State.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdarg.h>
#include <signal.h>
#include <setjmp.h>

#include "lua_addrs.h"

/* Forward declaration */
static void itbboot_log(const char *fmt, ...);

/* Signal handler for catching segfaults during injection */
static jmp_buf g_injection_jmp;
static void itbboot_segfault_handler(int sig) {
	itbboot_log("SIGSEGV during injection, bailing out");
	longjmp(g_injection_jmp, 1);
}

/* Lua 5.1 constants */
#define LUA_GLOBALSINDEX (-10002)

/* Function pointer types for calling static Lua functions in the game binary */
typedef int (*lua_pushcclosure_t)(void *L, void *f, int n);
typedef void (*lua_setfield_t)(void *L, int idx, const char *k);
typedef int (*lua_getfield_t)(void *L, int idx, const char *k);
typedef const char *(*lua_pushstring_t)(void *L, const char *s);
typedef void (*lua_pushnil_t)(void *L);
typedef void (*lua_pushvalue_t)(void *L, int idx);
typedef int (*lua_gettop_t)(void *L);
typedef void (*lua_settop_t)(void *L, int idx);
typedef const char *(*lua_tolstring_t)(void *L, int idx, size_t *len);
typedef void (*lua_call_t)(void *L, int nargs, int nresults);
typedef int (*lua_pcall_t)(void *L, int nargs, int nresults, int errfunc);
typedef int (*lua_error_t)(void *L);
typedef int (*luaL_error_t)(void *L, const char *fmt, ...);
typedef void *(*luaL_newstate_t)(void);

/* Original malloc from libc (definitely through PLT) */
static void *(*orig_malloc)(size_t size) = NULL;

/* Original fopen64 and fopen from libc */
static FILE *(*orig_fopen64)(const char *path, const char *mode) = NULL;
static FILE *(*orig_fopen)(const char *path, const char *mode) = NULL;

/* SDL_GL_SwapWindow from libSDL2 */
static void (*orig_sdl_gl_swapwindow)(void *window) = NULL;

/* State for tracking injection */
static void *g_lua_state = NULL;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_injection_done = 0;
static unsigned long g_frame_count = 0;
static unsigned long g_malloc_count = 0;
static FILE *g_log_fp = NULL;

/* Forward declarations */
static void itbboot_inject_loadlib(void *L);

/* Open the game log for debug output */
static FILE *get_log_fp(void) {
	if (!g_log_fp) {
		const char *home = getenv("HOME");
		if (!home) home = "/tmp";
		static char log_path[512];
		snprintf(log_path, sizeof(log_path), "%s/.local/share/IntoTheBreach/log.txt", home);
		g_log_fp = fopen(log_path, "a");
		if (!g_log_fp) {
			g_log_fp = stderr;
		}
	}
	return g_log_fp;
}

/* Log with timestamp and flush */
static void itbboot_log(const char *fmt, ...) {
	FILE *fp = get_log_fp();
	va_list ap;
	va_start(ap, fmt);
	fprintf(fp, "[itbboot] ");
	vfprintf(fp, fmt, ap);
	fprintf(fp, "\n");
	fflush(fp);
	va_end(ap);
}

/* Injected package.loadlib implementation using dlopen+dlsym
 * Signature: loadlib(path, symbol) -> function or (nil, message, "open"|"init")
 * The C closure receives arguments from Lua stack:
 * arg1=path (string), arg2=symbol (string)
 */
static int itbboot_loadlib_cfunc(void *L) {
	/* Get function pointers from static addresses in the game binary */
	lua_gettop_t lua_gettop = (lua_gettop_t)LUA_GETTOP;
	lua_tolstring_t lua_tolstring = (lua_tolstring_t)LUA_TOLSTRING;
	lua_pushnil_t lua_pushnil = (lua_pushnil_t)LUA_PUSHNIL;
	lua_pushstring_t lua_pushstring = (lua_pushstring_t)LUA_PUSHSTRING;
	lua_pushcclosure_t lua_pushcclosure = (lua_pushcclosure_t)LUA_PUSHCCLOSURE;

	int argc = lua_gettop(L);
	if (argc < 2) {
		lua_pushnil(L);
		lua_pushstring(L, "loadlib: not enough arguments");
		lua_pushstring(L, "open");
		return 3;
	}

	/* Get path (arg 1) */
	size_t path_len = 0;
	const char *path = lua_tolstring(L, 1, &path_len);
	if (!path) {
		lua_pushnil(L);
		lua_pushstring(L, "loadlib: first argument is not a string");
		lua_pushstring(L, "open");
		return 3;
	}

	/* Get symbol name (arg 2) */
	size_t sym_len = 0;
	const char *sym = lua_tolstring(L, 2, &sym_len);
	if (!sym) {
		lua_pushnil(L);
		lua_pushstring(L, "loadlib: second argument is not a string");
		lua_pushstring(L, "open");
		return 3;
	}

	/* Attempt dlopen */
	void *handle = dlopen(path, RTLD_NOW);
	if (!handle) {
		lua_pushnil(L);
		lua_pushstring(L, dlerror());
		lua_pushstring(L, "open");
		itbboot_log("dlopen failed for %s: %s", path, dlerror());
		return 3;
	}

	/* Attempt dlsym */
	void *func = dlsym(handle, sym);
	if (!func) {
		const char *err = dlerror();
		dlclose(handle);
		lua_pushnil(L);
		lua_pushstring(L, err);
		lua_pushstring(L, "init");
		itbboot_log("dlsym failed for %s: %s", sym, err);
		return 3;
	}

	/* Success: push the function using lua_pushcclosure with 0 upvalues */
	lua_pushcclosure(L, func, 0);
	itbboot_log("successfully loaded %s:%s", path, sym);

	return 1;
}

/* Inject the loadlib replacement into Lua's package.loadlib
 * Called once when the lua_State is first created.
 * Note: This is risky as we're calling into the game binary with an L we found via heuristics.
 * If the L is invalid, this will segfault. We try to be defensive.
 */
static void itbboot_inject_loadlib(void *L) {
	if (!L) {
		itbboot_log("inject_loadlib: L is null, skipping");
		return;
	}

	/* Safety check: verify that the pointer is in a reasonable range */
	uintptr_t L_addr = (uintptr_t)L;
	if (L_addr < 0x100000 || L_addr > 0x10000000000UL) {
		itbboot_log("inject_loadlib: invalid L address %p, skipping", L);
		return;
	}

	itbboot_log("inject_loadlib: attempting to patch L=%p", L);

	/* Install signal handler to catch crashes */
	struct sigaction sa, old_sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = itbboot_segfault_handler;
	sigaction(SIGSEGV, &sa, &old_sa);

	if (setjmp(g_injection_jmp) != 0) {
		/* We got a SIGSEGV, restore old handler and return */
		sigaction(SIGSEGV, &old_sa, NULL);
		itbboot_log("inject_loadlib: caught segfault, L is probably invalid");
		return;
	}

	/* Get function pointers from static addresses in the game binary.
	 * These are direct calls into the game's embedded Lua 5.1 */
	lua_getfield_t lua_getfield = (lua_getfield_t)LUA_GETFIELD;
	lua_setfield_t lua_setfield = (lua_setfield_t)LUA_SETFIELD;
	lua_pushcclosure_t lua_pushcclosure = (lua_pushcclosure_t)LUA_PUSHCCLOSURE;
	lua_gettop_t lua_gettop = (lua_gettop_t)LUA_GETTOP;
	lua_settop_t lua_settop = (lua_settop_t)LUA_SETTOP;

	/* Step 1: Get the package table from the globals */
	lua_getfield(L, LUA_GLOBALSINDEX, "package");

	/* Step 2: Check if we got something on the stack */
	int depth = lua_gettop(L);
	if (depth < 1) {
		itbboot_log("inject_loadlib: failed to get package table (depth=%d)", depth);
		sigaction(SIGSEGV, &old_sa, NULL);
		return;
	}

	itbboot_log("inject_loadlib: got package table at stack depth %d", depth);

	/* Step 3: Push our replacement C function */
	lua_pushcclosure(L, (void *)itbboot_loadlib_cfunc, 0);

	/* Step 4: Set it as package.loadlib */
	lua_setfield(L, -2, "loadlib");

	itbboot_log("inject_loadlib: successfully set package.loadlib");

	/* Step 5: Clean up the stack */
	lua_settop(L, -2);

	itbboot_log("inject_loadlib: injection complete!");

	/* Restore original signal handler */
	sigaction(SIGSEGV, &old_sa, NULL);
}

/* Find the main lua_State by examining the call stack
 * When scripts are being loaded, the lua_State is on the stack or accessible via registers.
 * We try a heuristic: search the stack for pointers that look like they could be a lua_State.
 *
 * A lua_State structure in Lua 5.1 starts with specific fields.
 * We look for patterns of pointers that match Lua's structure layout.
 */
static void *itbboot_search_lua_state_on_stack(void) {
	void **sp;
	int i;
	unsigned int count = 0;

	/* Get the current stack pointer (approximate) */
	__asm__("mov %%rsp, %0" : "=r"(sp));

	/* Search the stack for pointers that could be lua_State
	 * A valid lua_State pointer would point into the game's heap/bss
	 * and have a recognizable structure (high memory address range).
	 *
	 * Search deeper (512 values instead of 64) to find the actual L pointer.
	 */
	for (i = 0; i < 512; i++) {
		void *candidate = sp[i];
		if (candidate && (uintptr_t)candidate > 0x100000 && (uintptr_t)candidate < 0x10000000000UL) {
			/* This looks like a reasonable heap pointer. Could be lua_State. */
			count++;
			if (count <= 3) {
				itbboot_log("stack[%d]=%p (potential lua_State candidate)", i, candidate);
			}
			if (count == 1) {
				/* Return the first likely candidate */
				return candidate;
			}
		}
	}

	if (count > 0) {
		itbboot_log("found %u potential lua_State candidates on stack", count);
	}
	return NULL;
}

/* Try to find the lua_State by reading from known memory locations
 * We can try dereferencing pointers that the game might store globally.
 */
static void *itbboot_search_lua_state_in_memory(void) {
	/* Try some heuristics based on common Lua implementation patterns */

	/* Strategy 1: Look for a pointer in the game's BSS section
	 * The game probably stores the main lua_State* in static memory.
	 * We can try common locations: near the start of BSS, etc.
	 * For now, we use a brute-force approach scanning reasonable memory ranges.
	 */

	/* This is a fallback - in production, we'd use GDB to find the exact address */
	return NULL;
}

/* Hooked malloc - definitely called through PLT
 * Use this to bootstrap injection since malloc is called many times during init
 */
void *malloc(size_t size) {
	if (!orig_malloc) {
		orig_malloc = dlsym(RTLD_NEXT, "malloc");
	}

	/* First few malloc calls: try to find and inject loadlib */
	if (!g_injection_done && g_malloc_count < 1000) {
		g_malloc_count++;

		/* On malloc calls 100-200, try stack search for lua_State
		 * (gives Lua enough time to initialize) */
		if (g_malloc_count > 100 && g_malloc_count < 200 && !g_lua_state) {
			pthread_mutex_lock(&g_mutex);

			if (!g_lua_state && !g_injection_done) {
				/* Try to find lua_State on the stack */
				g_lua_state = itbboot_search_lua_state_on_stack();

				if (g_lua_state) {
					itbboot_log("found lua_State at malloc call #%lu: %p", g_malloc_count, g_lua_state);
					g_injection_done = 1;
					itbboot_inject_loadlib(g_lua_state);
				}
			}

			pthread_mutex_unlock(&g_mutex);
		}
	}

	/* Call the original malloc */
	return orig_malloc(size);
}

/* Hooked SDL_GL_SwapWindow - log frame count and lua_state for phase 3 verification */
void SDL_GL_SwapWindow(void *window) {
	if (!orig_sdl_gl_swapwindow) {
		orig_sdl_gl_swapwindow = dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");
	}

	g_frame_count++;

	/* Log periodically (every 300 frames, ~5 sec at 60fps) */
	if ((g_frame_count % 300) == 0) {
		itbboot_log("frame %lu, lua_state=%p", g_frame_count, g_lua_state);
	}

	/* Call the original SDL_GL_SwapWindow */
	if (orig_sdl_gl_swapwindow) {
		orig_sdl_gl_swapwindow(window);
	}
}

/* Initialization: log that we're loaded */
__attribute__((constructor))
static void itbboot_init(void) {
	/* Write to both the log file and stderr to ensure we see the message */
	FILE *fp = get_log_fp();
	fprintf(fp, "[itbboot] constructor running\n");
	fprintf(stderr, "[itbboot] libitbboot.so loaded and running\n");
	fflush(stderr);
	fflush(fp);
}
