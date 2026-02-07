#pragma once

extern "C" {
#include <lua.h>
}

class Vault;

// Register sandboxed filesystem and os bindings that route through vault:// URIs.
// Provides:
//   fs.read(path)               -> string|nil, err   -- read a vault file (scripts/assets)
//   fs.write(path, data)        -> bool, err          -- write a vault file
//   fs.list(prefix, recursive)  -> {string,...}        -- list vault files
//   fs.exists(path)             -> bool                -- check if a vault file exists
//   fs.stat(path)               -> table|nil           -- stat a vault file
//   fs.mkdir(path)              -> bool, err           -- create directory (no-op stub)
//   fs.delete(path)             -> bool, err           -- delete a vault file (no-op stub)
//
//   os.clock()                  -> number              -- CPU clock (safe)
//   os.time()                   -> number              -- current unix time (safe)
//   os.date(fmt, t)             -> string              -- formatted date (safe)
//   os.difftime(t2, t1)         -> number              -- time difference (safe)
//
//   require(modname)            -> value               -- loads from vault://Scripts/<mod>.lua
//   load(chunk, name, mode)     -> function|nil, err   -- string-only, text mode only
//   collectgarbage(opt)         -> ...                 -- only "collect" and "count" allowed
//
// Additionally reinforces the sandbox by ensuring these remain nil:
//   package, debug, io, loadfile, dofile, string.dump
//
// All paths are forced through the vault:// scheme. Direct disk access is blocked.
void registerLuaFSBindings(lua_State *L, Vault *vault);
