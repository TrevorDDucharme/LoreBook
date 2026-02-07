#include "LuaFSBindings.hpp"
#include "Vault.hpp"
#include "FileBackend.hpp"
#include "FileBackends/VaultFileBackend.hpp"
#include "LuaBindingDocs.hpp"
#include "LuaBindingDocsUtil.hpp"
#include <plog/Log.h>
#include <ctime>
#include <cstring>
#include <unordered_set>

// ── helpers ──────────────────────────────────────────────────────────────

// Retrieve the Vault* stored as upvalue 1 (lightuserdata)
static Vault *getVaultUpvalue(lua_State *L)
{
    return static_cast<Vault *>(lua_touserdata(L, lua_upvalueindex(1)));
}

// Ensure the path uses the vault:// scheme.  Accepts:
//   "Scripts/foo.lua"          -> vault://Scripts/foo.lua
//   "vault://Scripts/foo.lua"  -> vault://Scripts/foo.lua
//   "/Scripts/foo.lua"         -> vault://Scripts/foo.lua
static FileUri toVaultUri(const char *raw)
{
    std::string s(raw);
    // Already has a scheme?
    auto pos = s.find("://");
    if (pos != std::string::npos)
    {
        FileUri u = FileUri::parse(s);
        if (u.scheme != "vault")
        {
            // Force vault scheme — block file:// or other direct-access schemes
            u.scheme = "vault";
        }
        return u;
    }
    // Strip leading '/' for consistency
    if (!s.empty() && s.front() == '/') s.erase(s.begin());
    FileUri u;
    u.scheme = "vault";
    u.path = s;
    return u;
}

// ── fs.read(path) -> string|nil, err ─────────────────────────────────────

static int l_fs_read(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    Vault *v = getVaultUpvalue(L);
    if (!v) { lua_pushnil(L); lua_pushstring(L, "no vault"); return 2; }

    VaultFileBackend backend(v);
    FileUri uri = toVaultUri(path);
    FileResult res = backend.readFileSync(uri);
    if (res.ok)
    {
        lua_pushlstring(L, reinterpret_cast<const char *>(res.data.data()), res.data.size());
        return 1;
    }
    lua_pushnil(L);
    lua_pushstring(L, res.error.c_str());
    return 2;
}

// ── fs.write(path, data) -> bool, err ────────────────────────────────────

static int l_fs_write(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    size_t len = 0;
    const char *data = luaL_checklstring(L, 2, &len);
    Vault *v = getVaultUpvalue(L);
    if (!v) { lua_pushboolean(L, false); lua_pushstring(L, "no vault"); return 2; }

    VaultFileBackend backend(v);
    FileUri uri = toVaultUri(path);
    std::vector<uint8_t> bytes(data, data + len);
    FileResult res = backend.writeFileSync(uri, bytes);
    lua_pushboolean(L, res.ok);
    if (!res.ok) { lua_pushstring(L, res.error.c_str()); return 2; }
    return 1;
}

// ── fs.list(prefix, recursive) -> {string,...} ───────────────────────────

static int l_fs_list(lua_State *L)
{
    const char *prefix = luaL_optstring(L, 1, "");
    bool recursive = lua_toboolean(L, 2);
    Vault *v = getVaultUpvalue(L);
    if (!v) { lua_newtable(L); return 1; }

    VaultFileBackend backend(v);
    FileUri uri = toVaultUri(prefix);
    std::vector<std::string> entries = backend.list(uri, recursive);
    lua_createtable(L, (int)entries.size(), 0);
    int idx = 1;
    for (auto &e : entries)
    {
        lua_pushstring(L, e.c_str());
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

// ── fs.exists(path) -> bool ──────────────────────────────────────────────

static int l_fs_exists(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    Vault *v = getVaultUpvalue(L);
    if (!v) { lua_pushboolean(L, false); return 1; }

    VaultFileBackend backend(v);
    FileUri uri = toVaultUri(path);
    FileResult res = backend.readFileSync(uri);
    lua_pushboolean(L, res.ok && !res.data.empty());
    return 1;
}

// ── fs.stat(path) -> table|nil ───────────────────────────────────────────

static int l_fs_stat(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    Vault *v = getVaultUpvalue(L);
    if (!v) { lua_pushnil(L); return 1; }

    VaultFileBackend backend(v);
    FileUri uri = toVaultUri(path);
    FileResult res = backend.stat(uri);
    if (!res.ok) { lua_pushnil(L); return 1; }

    lua_newtable(L);
    lua_pushstring(L, uri.toString().c_str());
    lua_setfield(L, -2, "uri");
    lua_pushboolean(L, true);
    lua_setfield(L, -2, "exists");
    return 1;
}

// ── fs.mkdir(path) -> bool, err  (stub) ──────────────────────────────────

static int l_fs_mkdir(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    Vault *v = getVaultUpvalue(L);
    if (!v) { lua_pushboolean(L, false); lua_pushstring(L, "no vault"); return 2; }

    VaultFileBackend backend(v);
    FileUri uri = toVaultUri(path);
    FileResult res = backend.createDirectory(uri);
    lua_pushboolean(L, res.ok);
    if (!res.ok) { lua_pushstring(L, res.error.c_str()); return 2; }
    return 1;
}

// ── fs.delete(path) -> bool, err  (stub) ─────────────────────────────────

static int l_fs_delete(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    Vault *v = getVaultUpvalue(L);
    if (!v) { lua_pushboolean(L, false); lua_pushstring(L, "no vault"); return 2; }

    VaultFileBackend backend(v);
    FileUri uri = toVaultUri(path);
    FileResult res = backend.deleteFile(uri);
    lua_pushboolean(L, res.ok);
    if (!res.ok) { lua_pushstring(L, res.error.c_str()); return 2; }
    return 1;
}

// ── Safe os.* replacements ───────────────────────────────────────────────

static int l_os_clock(lua_State *L)
{
    lua_pushnumber(L, (double)clock() / (double)CLOCKS_PER_SEC);
    return 1;
}

static int l_os_time(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)time(nullptr));
    return 1;
}

static int l_os_date(lua_State *L)
{
    const char *fmt = luaL_optstring(L, 1, "%c");
    time_t t;
    if (lua_isnoneornil(L, 2))
        t = time(nullptr);
    else
        t = (time_t)luaL_checkinteger(L, 2);

    struct tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char buf[256];
    size_t n = strftime(buf, sizeof(buf), fmt, &tmBuf);
    lua_pushlstring(L, buf, n);
    return 1;
}

static int l_os_difftime(lua_State *L)
{
    lua_Number t2 = luaL_checknumber(L, 1);
    lua_Number t1 = luaL_checknumber(L, 2);
    lua_pushnumber(L, t2 - t1);
    return 1;
}

// ── Sandboxed require() — loads modules from vault://Scripts/ ────────────
// Upvalue 1 = vault lightuserdata
// Uses a per-state "loaded" registry to avoid circular requires.

static int l_safe_require(lua_State *L)
{
    const char *modname = luaL_checkstring(L, 1);
    Vault *v = static_cast<Vault *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!v) return luaL_error(L, "require: no vault available");

    // Check if already loaded (registry key: "_VAULT_LOADED")
    lua_getfield(L, LUA_REGISTRYINDEX, "_VAULT_LOADED");
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "_VAULT_LOADED");
    }
    lua_getfield(L, -1, modname);
    if (!lua_isnil(L, -1))
    {
        // Already loaded — return cached result
        return 1;
    }
    lua_pop(L, 1); // pop nil

    // Convert module name to vault path: "foo.bar" -> "Scripts/foo/bar.lua"
    std::string path = "Scripts/";
    for (const char *p = modname; *p; ++p)
    {
        if (*p == '.')
            path += '/';
        else
            path += *p;
    }
    path += ".lua";

    // Load from vault
    VaultFileBackend backend(v);
    FileUri uri;
    uri.scheme = "vault";
    uri.path = path;
    FileResult res = backend.readFileSync(uri);
    if (!res.ok || res.data.empty())
        return luaL_error(L, "require: module '%s' not found in vault (tried %s)", modname, path.c_str());

    std::string code(res.data.begin(), res.data.end());
    std::string chunkName = std::string("@vault://") + path;

    // Compile
    if (luaL_loadbuffer(L, code.c_str(), code.size(), chunkName.c_str()) != LUA_OK)
    {
        const char *err = lua_tostring(L, -1);
        return luaL_error(L, "require: error loading module '%s': %s", modname, err ? err : "unknown");
    }

    // Execute — module returns value(s); we take the first
    if (lua_pcall(L, 0, 1, 0) != LUA_OK)
    {
        const char *err = lua_tostring(L, -1);
        return luaL_error(L, "require: error running module '%s': %s", modname, err ? err : "unknown");
    }

    // If module returned nil, store true as a sentinel so we don't reload
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }

    // Cache in _VAULT_LOADED
    lua_pushvalue(L, -1); // dup result
    // stack: ... loaded_table, result, result
    lua_setfield(L, -3, modname); // loaded_table[modname] = result

    // Return the result (leave one copy on stack)
    return 1;
}

// ── Safe load() — only accepts string sources, no file loading ───────────

static int l_safe_load(lua_State *L)
{
    // Signature: load(chunk [, chunkname [, mode [, env]]])
    // We only accept string chunks (no function chunks which could read files)
    size_t len = 0;
    const char *chunk = luaL_checklstring(L, 1, &len);
    const char *chunkname = luaL_optstring(L, 2, "=(load)");

    // mode: only allow "t" (text) to prevent loading binary bytecode
    const char *mode = luaL_optstring(L, 3, "t");
    if (mode && strchr(mode, 'b'))
        return luaL_error(L, "load: binary mode is not allowed in sandboxed environment");

    int status = luaL_loadbufferx(L, chunk, len, chunkname, "t");
    if (status != LUA_OK)
    {
        // Return nil, error
        lua_pushnil(L);
        lua_insert(L, -2); // push nil before error message
        return 2;
    }

    // If caller provided an env table (arg 4), set it as the first upvalue
    if (!lua_isnoneornil(L, 4))
    {
        lua_pushvalue(L, 4);
        if (!lua_setupvalue(L, -2, 1))
            lua_pop(L, 1); // pop if setupvalue failed
    }

    return 1; // return the function
}

// ── Safe collectgarbage — only "count" and "collect" allowed ─────────────

static int l_safe_collectgarbage(lua_State *L)
{
    const char *opt = luaL_optstring(L, 1, "collect");
    if (strcmp(opt, "collect") == 0)
    {
        lua_gc(L, LUA_GCCOLLECT, 0);
        return 0;
    }
    if (strcmp(opt, "count") == 0)
    {
        int kb = lua_gc(L, LUA_GCCOUNT, 0);
        int bytes = lua_gc(L, LUA_GCCOUNTB, 0);
        lua_pushnumber(L, (double)kb + (double)bytes / 1024.0);
        return 1;
    }
    return luaL_error(L, "collectgarbage: only 'collect' and 'count' are allowed in sandboxed environment");
}

// ── Registration ─────────────────────────────────────────────────────────

void registerLuaFSBindings(lua_State *L, Vault *vault)
{
    // ── fs table ─────────────────────────────────────────────────────────
    lua_newtable(L);

    // Helper macro: push vault lightuserdata, create closure, set as field
    #define FS_FUNC(name, cfunc) \
        lua_pushlightuserdata(L, vault); \
        lua_pushcclosure(L, cfunc, 1);   \
        lua_setfield(L, -2, name)

    FS_FUNC("read",   l_fs_read);
    FS_FUNC("write",  l_fs_write);
    FS_FUNC("list",   l_fs_list);
    FS_FUNC("exists", l_fs_exists);
    FS_FUNC("stat",   l_fs_stat);
    FS_FUNC("mkdir",  l_fs_mkdir);
    FS_FUNC("delete", l_fs_delete);

    #undef FS_FUNC

    lua_setglobal(L, "fs");

    // ── Docs for fs ──────────────────────────────────────────────────────
    LuaBindingDocs::get().registerDoc("fs.read", "read(path) -> string|nil, err",
        "Read a vault file. Path is relative to vault root (e.g. 'Scripts/foo.lua').",
        R"(
local code, err = fs.read("Scripts/myHelper.lua")
if code then
    print("Read " .. #code .. " bytes")
else
    print("Error: " .. err)
end
)", __FILE__);

    LuaBindingDocs::get().registerDoc("fs.write", "write(path, data) -> bool, err",
        "Write data to a vault file. Creates or updates the script/asset.",
        R"(
local ok, err = fs.write("Scripts/generated.lua", "-- auto-generated\nreturn 42\n")
if not ok then print("Write failed: " .. err) end
)", __FILE__);

    LuaBindingDocs::get().registerDoc("fs.list", "list(prefix, recursive) -> {string,...}",
        "List vault files matching an optional prefix.",
        R"(
local scripts = fs.list("Scripts/")
for _, name in ipairs(scripts) do
    print(name)
end
)", __FILE__);

    LuaBindingDocs::get().registerDoc("fs.exists", "exists(path) -> bool",
        "Check whether a vault file exists.",
        R"(
if fs.exists("Scripts/myHelper.lua") then
    print("Found it!")
end
)", __FILE__);

    LuaBindingDocs::get().registerDoc("fs.stat", "stat(path) -> table|nil",
        "Get metadata for a vault file. Returns a table with 'uri' and 'exists' fields, or nil.",
        R"(
local info = fs.stat("Scripts/myHelper.lua")
if info then print("URI: " .. info.uri) end
)", __FILE__);

    LuaBindingDocs::get().registerDoc("fs.mkdir", "mkdir(path) -> bool, err",
        "Create a directory in the vault namespace (currently a stub).",
        R"(
local ok, err = fs.mkdir("Assets/sprites")
)", __FILE__);

    LuaBindingDocs::get().registerDoc("fs.delete", "delete(path) -> bool, err",
        "Delete a vault file (currently a stub).",
        R"(
local ok, err = fs.delete("Scripts/old.lua")
)", __FILE__);

    // Enforce docs
    LuaBindingDocsUtil::enforceTableHasDocs(L, "fs");

    // ── Safe os table ────────────────────────────────────────────────────
    // Replace the nil'ed-out os global with a safe subset
    lua_newtable(L);

    lua_pushcfunction(L, l_os_clock);    lua_setfield(L, -2, "clock");
    lua_pushcfunction(L, l_os_time);     lua_setfield(L, -2, "time");
    lua_pushcfunction(L, l_os_date);     lua_setfield(L, -2, "date");
    lua_pushcfunction(L, l_os_difftime); lua_setfield(L, -2, "difftime");

    lua_setglobal(L, "os");

    // Docs for os
    LuaBindingDocs::get().registerDoc("os.clock", "clock() -> number",
        "Returns an approximation of the CPU time used by the program, in seconds.",
        R"(
local start = os.clock()
-- do work
print("Elapsed: " .. (os.clock() - start) .. "s")
)", __FILE__);

    LuaBindingDocs::get().registerDoc("os.time", "time() -> number",
        "Returns the current Unix timestamp.",
        R"(
print("Now: " .. os.time())
)", __FILE__);

    LuaBindingDocs::get().registerDoc("os.date", "date(fmt, t) -> string",
        "Format a time value. Uses strftime format strings.",
        R"(
print(os.date("%Y-%m-%d %H:%M:%S"))
print(os.date("%A", os.time()))
)", __FILE__);

    LuaBindingDocs::get().registerDoc("os.difftime", "difftime(t2, t1) -> number",
        "Returns the difference in seconds between two time values.",
        R"(
local t1 = os.time()
-- wait
local t2 = os.time()
print("Diff: " .. os.difftime(t2, t1))
)", __FILE__);

    // Enforce docs for os
    LuaBindingDocsUtil::enforceTableHasDocs(L, "os");

    // ── Vault-routed require() ───────────────────────────────────────────
    // Replaces stdlib require with one that loads modules from vault://Scripts/
    lua_pushlightuserdata(L, vault);
    lua_pushcclosure(L, l_safe_require, 1);
    lua_setglobal(L, "require");

    // ── Safe load() — string-only, text mode only ────────────────────────
    lua_pushcfunction(L, l_safe_load);
    lua_setglobal(L, "load");

    // ── Safe collectgarbage — only count and collect ─────────────────────
    lua_pushcfunction(L, l_safe_collectgarbage);
    lua_setglobal(L, "collectgarbage");

    // ── Ensure package/debug/string.dump remain nil ──────────────────────
    // (setupSandbox already clears these but guard against re-registration)
    lua_pushnil(L); lua_setglobal(L, "package");
    lua_pushnil(L); lua_setglobal(L, "debug");
    lua_getglobal(L, "string");
    if (lua_istable(L, -1))
    {
        lua_pushnil(L);
        lua_setfield(L, -2, "dump");
    }
    lua_pop(L, 1);
}
