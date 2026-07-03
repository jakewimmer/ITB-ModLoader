#include "blob.h"
#include "itbsdl.h"

#include <lua.hpp>
#include <LuaBridge/LuaBridge.h>

#include <SDL2/SDL.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>

using namespace luabridge;
using namespace SDL;

namespace POSIX {

/* Get XDG directory paths for common known-folder IDs.
 * Maps Windows CSIDL constants to Linux XDG paths.
 *
 * The loader uses these IDs (search ITB-ModLoader for actual calls).
 * We map the ones that appear and error on unmapped IDs.
 */
std::string getKnownFolder(int id) {
  const char *home = std::getenv("HOME");
  if (!home) {
    return "";
  }

  const char *xdg_data_home = std::getenv("XDG_DATA_HOME");
  const char *xdg_config_home = std::getenv("XDG_CONFIG_HOME");
  const char *xdg_cache_home = std::getenv("XDG_CACHE_HOME");

  std::string base_data =
      xdg_data_home ? xdg_data_home : std::string(home) + "/.local/share";
  std::string base_config =
      xdg_config_home ? xdg_config_home : std::string(home) + "/.config";
  std::string base_cache =
      xdg_cache_home ? xdg_cache_home : std::string(home) + "/.cache";

  /* Map Windows CSIDL constants (from shlobj.h) to XDG paths */
  switch (id) {
  /* CSIDL_APPDATA = 0x1a (26) */
  case 26:
    return base_data;

  /* CSIDL_LOCAL_APPDATA = 0x1c (28) */
  case 28:
    return base_data;

  /* CSIDL_PERSONAL (Documents) = 0x5 (5) */
  case 5:
    return base_data;

  /* CSIDL_DESKTOPDIRECTORY = 0x10 (16) */
  case 16:
    return std::string(home) + "/Desktop";

  /* CSIDL_FAVORITES = 0x6 (6) */
  case 6:
    return base_config;

  default:
    /* Unknown folder ID; error clearly */
    fprintf(stderr, "getKnownFolder: unknown folder ID %d\n", id);
    return "";
  }
}

void log(const std::string &line) {
  const char *home = std::getenv("HOME");
  if (!home) {
    fprintf(stderr, "log: HOME not set\n");
    return;
  }

  std::string logpath = std::string(home) + "/.local/share/IntoTheBreach/log.txt";

  /* Ensure directory exists */
  std::string dir = std::string(home) + "/.local/share/IntoTheBreach";
  mkdir(dir.c_str(), 0755);

  /* Append to log file */
  std::ofstream logfile(logpath, std::ios::app);
  if (!logfile) {
    fprintf(stderr, "log: failed to open %s\n", logpath.c_str());
    return;
  }

  logfile << line << "\n";
  logfile.close();
}

bool isshiftdown() {
  SDL_Keymod mod = SDL_GetModState();
  return (mod & KMOD_SHIFT) != 0;
}

double mtime(const std::string &filename) {
  struct stat st = {};
  if (stat(filename.c_str(), &st) != 0) {
    return 0.0;
  }
  return static_cast<double>(st.st_mtime);
}

void mkdir(const std::string &path) {
  /* Recursively create directories. Not ideal, but simple. */
  std::string dir = path;
  size_t pos = 0;
  while ((pos = dir.find('/', pos + 1)) != std::string::npos) {
    std::string subdir = dir.substr(0, pos);
    ::mkdir(subdir.c_str(), 0755);
  }
  ::mkdir(dir.c_str(), 0755);
}

void messagebox(const std::string &title, const std::string &message) {
  SDL_Window *window = SDL_GL_GetCurrentWindow();
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(),
                           message.c_str(), window);
}

} // namespace POSIX

/* Lua wrappers for os functions */
static int lua_os_listall(lua_State *L) {
  const char *dirname = luaL_checkstring(L, 1);
  lua_newtable(L);

  DIR *dir = opendir(dirname);
  if (!dir) {
    return 1; /* return empty table */
  }

  int index = 1;
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    lua_pushnumber(L, index++);
    lua_pushstring(L, entry->d_name);
    lua_settable(L, -3);
  }

  closedir(dir);
  return 1;
}

static int lua_os_listfiles(lua_State *L) {
  const char *dirname = luaL_checkstring(L, 1);
  lua_newtable(L);

  DIR *dir = opendir(dirname);
  if (!dir) {
    return 1; /* return empty table */
  }

  int index = 1;
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    /* Check if it's a regular file */
    std::string full_path = std::string(dirname) + "/" + entry->d_name;
    struct stat st = {};
    if (stat(full_path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
      lua_pushnumber(L, index++);
      lua_pushstring(L, entry->d_name);
      lua_settable(L, -3);
    }
  }

  closedir(dir);
  return 1;
}

static int lua_os_listdirs(lua_State *L) {
  const char *dirname = luaL_checkstring(L, 1);
  lua_newtable(L);

  DIR *dir = opendir(dirname);
  if (!dir) {
    return 1; /* return empty table */
  }

  int index = 1;
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    /* Check if it's a directory */
    std::string full_path = std::string(dirname) + "/" + entry->d_name;
    struct stat st = {};
    if (stat(full_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
      lua_pushnumber(L, index++);
      lua_pushstring(L, entry->d_name);
      lua_settable(L, -3);
    }
  }

  closedir(dir);
  return 1;
}

static int lua_os_mkdir(lua_State *L) {
  const char *path = luaL_checkstring(L, 1);
  POSIX::mkdir(path);
  return 0;
}

static int lua_os_mtime(lua_State *L) {
  const char *filename = luaL_checkstring(L, 1);
  lua_pushnumber(L, POSIX::mtime(filename));
  return 1;
}

static int lua_os_getKnownFolder(lua_State *L) {
  int id = luaL_checkint(L, 1);
  std::string result = POSIX::getKnownFolder(id);
  lua_pushstring(L, result.c_str());
  return 1;
}

static int lua_os_log(lua_State *L) {
  const char *message = luaL_checkstring(L, 1);
  POSIX::log(message);
  return 0;
}

static int lua_os_isshiftdown(lua_State *L) {
  lua_pushboolean(L, POSIX::isshiftdown());
  return 1;
}

static int lua_os_messagebox(lua_State *L) {
  const char *title = luaL_checkstring(L, 1);
  const char *message = luaL_checkstring(L, 2);
  POSIX::messagebox(title, message);
  return 0;
}

/* Blob implementations */
namespace SDL {

BlobFromFile::BlobFromFile(const std::string &filename) : owned_data(nullptr) {
  FILE *file = fopen(filename.c_str(), "rb");
  if (!file) {
    data = nullptr;
    length = 0;
    return;
  }

  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);
  fseek(file, 0, SEEK_SET);

  owned_data = new uint8_t[size];
  length = size;
  size_t read_bytes = fread(owned_data, 1, size, file);
  fclose(file);

  if (read_bytes != size) {
    delete[] owned_data;
    owned_data = nullptr;
    data = nullptr;
    length = 0;
    return;
  }

  data = owned_data;
}

BlobFromFile::~BlobFromFile() {
  if (owned_data) {
    delete[] owned_data;
  }
}

ResourceDat::ResourceDat(const std::string &filename) : filename(filename) {
  reload();
}

void ResourceDat::reload() {
  index.clear();

  FILE *file = fopen(filename.c_str(), "rb");
  if (!file) {
    return;
  }

  uint32_t indexSize = 0;
  if (fread(&indexSize, sizeof(indexSize), 1, file) != 1) {
    fclose(file);
    return;
  }

  uint32_t *indexOffsets = new uint32_t[indexSize];
  if (fread(indexOffsets, sizeof(indexOffsets[0]), indexSize, file) !=
      indexSize) {
    delete[] indexOffsets;
    fclose(file);
    return;
  }

  for (uint32_t i = 0; i < indexSize; i++) {
    uint32_t size = 0, namesize = 0;

    fseek(file, indexOffsets[i], SEEK_SET);
    if (fread(&size, sizeof(size), 1, file) != 1) {
      continue;
    }
    if (fread(&namesize, sizeof(namesize), 1, file) != 1) {
      continue;
    }

    std::vector<char> vec(namesize);
    if (fread(vec.data(), namesize, 1, file) != 1) {
      continue;
    }

    std::string name(vec.begin(), vec.end());
    size_t current_pos = ftell(file);
    index[name] = FileInfo(current_pos, size);
  }

  delete[] indexOffsets;
  fclose(file);
}

BlobFromResourceDat::BlobFromResourceDat(const ResourceDat *dat,
                                         const std::string &entryname)
    : owned_data(nullptr) {
  if (!dat) {
    data = nullptr;
    length = 0;
    return;
  }

  auto iter = dat->index.find(entryname);
  if (iter == dat->index.end()) {
    data = nullptr;
    length = 0;
    return;
  }

  const ResourceDat::FileInfo &info = iter->second;

  FILE *file = fopen(dat->filename.c_str(), "rb");
  if (!file) {
    data = nullptr;
    length = 0;
    return;
  }

  fseek(file, info.offset, SEEK_SET);

  owned_data = new uint8_t[info.size];
  length = info.size;
  size_t read_bytes = fread(owned_data, 1, info.size, file);
  fclose(file);

  if (read_bytes != info.size) {
    delete[] owned_data;
    owned_data = nullptr;
    data = nullptr;
    length = 0;
    return;
  }

  data = owned_data;
}

BlobFromResourceDat::~BlobFromResourceDat() {
  if (owned_data) {
    delete[] owned_data;
  }
}

} // namespace SDL

static void install_os_namespace(lua_State *L) {
  getGlobalNamespace(L)
      .beginNamespace("os")

      /* os.listall, os.listfiles, os.listdirs */
      .addCFunction("listall", &lua_os_listall)
      .addCFunction("listfiles", &lua_os_listfiles)
      .addCFunction("listdirs", &lua_os_listdirs)

      /* os.mkdir, os.mtime */
      .addCFunction("mkdir", &lua_os_mkdir)
      .addCFunction("mtime", &lua_os_mtime)

      /* os.getKnownFolder, os.log, os.isshiftdown, os.messagebox */
      .addCFunction("getKnownFolder", &lua_os_getKnownFolder)
      .addCFunction("log", &lua_os_log)
      .addCFunction("isshiftdown", &lua_os_isshiftdown)
      .addCFunction("messagebox", &lua_os_messagebox)

      .endNamespace();

  /* Register sdl.resourceDat and blob functions */
  getGlobalNamespace(L)
      .beginNamespace("sdl")

      /* resourceDat: archive reader */
      .beginClass<ResourceDat>("resourceDat")
      .addConstructor<void(*)(const std::string &)>()
      .addFunction("reload", &ResourceDat::reload)
      .endClass()

      /* Blob: base class for in-memory data */
      .beginClass<Blob>("blob")
      .addData("data", &Blob::data, false)
      .addData("length", &Blob::length, false)
      .endClass()

      /* BlobFromFile: load entire file into memory */
      .deriveClass<BlobFromFile, Blob>("blobFromFile")
      .addConstructor<void(*)(const std::string &)>()
      .endClass()

      /* BlobFromResourceDat: load entry from resource.dat archive */
      .deriveClass<BlobFromResourceDat, Blob>("blobFromResourceDat")
      .addConstructor<void(*)(const ResourceDat *, const std::string &)>()
      .endClass()

      .endNamespace();
}

extern "C" void register_os_namespace(lua_State *L) {
  install_os_namespace(L);
}
