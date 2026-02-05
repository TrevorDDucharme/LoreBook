#include "LuaVaultBindings.hpp"
#include "Vault.hpp"
#include <plog/Log.h>

// Helper: push vector<string> as lua array
static void pushStringArray(lua_State *L, const std::vector<std::string> &arr)
{
    lua_newtable(L);
    int idx = 1;
    for (auto &s : arr)
    {
        lua_pushstring(L, s.c_str());
        lua_rawseti(L, -2, idx++);
    }
}

static int l_vault_getNode(lua_State *L)
{
    Vault *v = *static_cast<Vault **>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!v) { lua_pushnil(L); return 1; }
    if (!lua_isinteger(L, 1)) { lua_pushnil(L); return 1; }
    int64_t id = (int64_t)lua_tointeger(L, 1);
    auto r = v->getItemPublic(id);
    if (r.id == -1) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushinteger(L, r.id); lua_setfield(L, -2, "id");
    lua_pushstring(L, r.name.c_str()); lua_setfield(L, -2, "name");
    lua_pushstring(L, r.content.c_str()); lua_setfield(L, -2, "content");
    pushStringArray(L, v->getTagsOfPublic(r.id)); lua_setfield(L, -2, "tags");
    return 1;
}

static int l_vault_getContent(lua_State *L)
{
    Vault *v = *static_cast<Vault **>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!v) { lua_pushnil(L); return 1; }
    if (!lua_isinteger(L, 1)) { lua_pushnil(L); return 1; }
    int64_t id = (int64_t)lua_tointeger(L, 1);
    auto r = v->getItemPublic(id);
    lua_pushstring(L, r.content.c_str());
    return 1;
}

static int l_vault_getTags(lua_State *L)
{
    Vault *v = *static_cast<Vault **>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!v) { lua_pushnil(L); return 1; }
    if (!lua_isinteger(L, 1)) { lua_pushnil(L); return 1; }
    int64_t id = (int64_t)lua_tointeger(L, 1);
    auto tags = v->getTagsOfPublic(id);
    pushStringArray(L, tags);
    return 1;
}

static int l_vault_currentNodeID(lua_State *L)
{
    Vault *v = *static_cast<Vault **>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!v) { lua_pushinteger(L, -1); return 1; }
    lua_pushinteger(L, v->getSelectedItemID());
    return 1;
}

static int l_vault_currentUserID(lua_State *L)
{
    Vault *v = *static_cast<Vault **>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!v) { lua_pushinteger(L, -1); return 1; }
    lua_pushinteger(L, v->getCurrentUserID());
    return 1;
}

void registerLuaVaultBindings(lua_State *L, Vault *vault)
{
    // store vault pointer in a userdata and use as upvalue to functions
    Vault **ud = static_cast<Vault **>(lua_newuserdata(L, sizeof(Vault *)));
    *ud = vault;

    lua_newtable(L); // vault table

    lua_pushvalue(L, -2); // push userdata
    lua_pushcclosure(L, l_vault_getNode, 1);
    lua_setfield(L, -2, "getNode");

    lua_pushvalue(L, -2);
    lua_pushcclosure(L, l_vault_getContent, 1);
    lua_setfield(L, -2, "getContent");

    lua_pushvalue(L, -2);
    lua_pushcclosure(L, l_vault_getTags, 1);
    lua_setfield(L, -2, "getTags");

    lua_pushvalue(L, -2);
    lua_pushcclosure(L, l_vault_currentNodeID, 1);
    lua_setfield(L, -2, "currentNodeID");

    lua_pushvalue(L, -2);
    lua_pushcclosure(L, l_vault_currentUserID, 1);
    lua_setfield(L, -2, "currentUserID");

    // Set global 'vault'
    lua_setglobal(L, "vault");

    // The userdata remains on the stack top; pop it
    lua_pop(L, 1);
}
