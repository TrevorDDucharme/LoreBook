#pragma once

// Run a lua-based doc-test that registers bindings into a temporary lua_State
// and enforces documentation presence. Throws std::runtime_error on failure.
void RunLuaDocChecks();
