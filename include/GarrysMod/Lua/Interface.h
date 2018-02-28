#ifndef GARRYSMOD_LUA_INTERFACE_H
#define GARRYSMOD_LUA_INTERFACE_H

#include "LuaBase.h"

struct lua_State
{
    unsigned char             _ignore_this_common_lua_header_[69];
    GarrysMod::Lua::ILuaBase* luabase;
};

#ifndef GMOD
    #ifdef _WIN32
        #define DLL_EXPORT extern "C" __declspec( dllexport )
    #else
        #define DLL_EXPORT extern "C" __attribute__((visibility("default")))
    #endif

#ifdef GMOD_ALLOW_DEPRECATED
    // Stop using this and use LUA_FUNCTION!
    #define LUA ( state->luabase )

    #define GMOD_MODULE_OPEN()  DLL_EXPORT int gmod13_open( lua_State* state )
    #define GMOD_MODULE_CLOSE() DLL_EXPORT int gmod13_close( lua_State* state )
#else
    #define GMOD_MODULE_OPEN()                     \
        int gmod13_open__Imp( ILuaBase* LUA );     \
        DLL_EXPORT int gmod13_open( lua_State* L ) \
        {                                          \
            return gmod13_open__Imp( L->luabase ); \
        }                                          \
        int gmod13_open__Imp( ILuaBase* LUA )

    #define GMOD_MODULE_CLOSE()                     \
        int gmod13_close__Imp( ILuaBase* LUA );     \
        DLL_EXPORT int gmod13_close( lua_State* L ) \
        {                                           \
            return gmod13_close__Imp( L->luabase ); \
        }                                           \
        int gmod13_close__Imp( ILuaBase* LUA )

    #define LUA_FUNCTION( FUNC )          \
        int FUNC##__Imp( ILuaBase* LUA ); \
        int FUNC( lua_State* L )          \
        {                                 \
            ILuaBase* LUA = L->luabase;   \
            LUA->SetState(L);             \
            return FUNC##__Imp( LUA );    \
        }                                 \
        int FUNC##__Imp( ILuaBase* LUA )
#endif

#endif

#endif
