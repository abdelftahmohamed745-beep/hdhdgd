#include "nimbus/script.hpp"

#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

#include "nimbus/log.hpp"

#if NIMBUS_HAS_LUA
#include "lua_abi.hpp"
#endif

namespace nimbus::script {
namespace {

using LuaNum = double;
using LuaCtx = long long;
using LuaCFn = int (*)(lua_State*);
using LuaKFn = int (*)(lua_State*, int, LuaCtx);
using LuaHookFn = void (*)(lua_State*, void*);

struct DynamicLuaApi {
  bool loaded = false;
  bool isLua54 = false;
  int registryIndex = -10000;
  int globalsIndex = -10002;
  std::string versionLabel;
  void* dlHandle = nullptr;

  lua_State* (*L_newstate)() = nullptr;
  void (*L_openlibs)(lua_State*) = nullptr;
  void (*close)(lua_State*) = nullptr;
  int (*L_loadbufferx)(lua_State*, const char*, size_t, const char*, const char*) = nullptr;
  int (*L_loadfilex)(lua_State*, const char*, const char*) = nullptr;
  int (*pcallk)(lua_State*, int, int, int, LuaCtx, LuaKFn) = nullptr;
  int (*pcall51)(lua_State*, int, int, int) = nullptr;
  int (*getglobal)(lua_State*, const char*) = nullptr;
  void (*setglobal)(lua_State*, const char*) = nullptr;
  void (*getfield)(lua_State*, int, const char*) = nullptr;
  void (*setfield)(lua_State*, int, const char*) = nullptr;
  void (*createtable)(lua_State*, int, int) = nullptr;
  void (*pushnumber)(lua_State*, LuaNum) = nullptr;
  void (*pushstring)(lua_State*, const char*) = nullptr;
  void (*pushlstring)(lua_State*, const char*, size_t) = nullptr;
  void (*pushnil)(lua_State*) = nullptr;
  void (*pushlightuserdata)(lua_State*, void*) = nullptr;
  void (*pushcclosure)(lua_State*, LuaCFn, int) = nullptr;
  void* (*touserdata)(lua_State*, int) = nullptr;
  LuaNum (*tonumberx)(lua_State*, int, int*) = nullptr;
  LuaNum (*tonumber51)(lua_State*, int) = nullptr;
  int (*isnumber51)(lua_State*, int) = nullptr;
  const char* (*tolstring)(lua_State*, int, size_t*) = nullptr;
  int (*type)(lua_State*, int) = nullptr;
  void (*settop)(lua_State*, int) = nullptr;
  int (*sethook)(lua_State*, LuaHookFn, int, int) = nullptr;
  int (*L_error)(lua_State*, const char*, ...) = nullptr;
  int (*jit_setmode)(lua_State*, int, int) = nullptr;

  int call(lua_State* L, int nargs, int nresults) const {
    if (pcallk) return pcallk(L, nargs, nresults, 0, 0, nullptr);
    return pcall51 ? pcall51(L, nargs, nresults, 0) : -1;
  }
  void getGlobal(lua_State* L, const char* name) const {
    if (getglobal) (void)getglobal(L, name);
    else if (getfield) getfield(L, globalsIndex, name);
  }
  void setGlobal(lua_State* L, const char* name) const {
    if (setglobal) setglobal(L, name);
    else if (setfield) setfield(L, globalsIndex, name);
  }
  bool isNumber(lua_State* L, int idx) const {
    if (isnumber51) return isnumber51(L, idx) != 0;
    int isNum = 0;
    if (tonumberx) (void)tonumberx(L, idx, &isNum);
    return isNum != 0;
  }
  double toNumber(lua_State* L, int idx) const {
    if (tonumberx) return tonumberx(L, idx, nullptr);
    return tonumber51 ? tonumber51(L, idx) : 0.0;
  }
  const char* toString(lua_State* L, int idx) const {
    return tolstring ? tolstring(L, idx, nullptr) : nullptr;
  }
  void pop(lua_State* L, int n) const {
    if (settop) settop(L, -(n)-1);
  }
  int upvalueIndex(int i) const { return (isLua54 ? registryIndex : globalsIndex) - i; }
};

std::mutex& stateApiMutex() {
  static std::mutex m;
  return m;
}
std::unordered_map<lua_State*, const DynamicLuaApi*>& stateApiMap() {
  static std::unordered_map<lua_State*, const DynamicLuaApi*> m;
  return m;
}

const DynamicLuaApi* apiForState(lua_State* L) {
  std::lock_guard<std::mutex> lock(stateApiMutex());
  auto it = stateApiMap().find(L);
  return it != stateApiMap().end() ? it->second : nullptr;
}

const DynamicLuaApi& loadDynamicLuaApi() {
  static DynamicLuaApi api = []() -> DynamicLuaApi {
    DynamicLuaApi result;
#if !defined(_WIN32)
    const char* candidates[] = {
        "liblua5.4.so.0", "liblua5.4.so", "libluajit-5.1.so.2", "libluajit-5.1.so",
        "liblua5.1.so.0", "liblua.so"};
    for (const char* name : candidates) {
      void* h = ::dlopen(name, RTLD_LAZY | RTLD_LOCAL);
      if (!h) continue;
      auto sym = [&](const char* s) { return ::dlsym(h, s); };
      result.L_newstate = reinterpret_cast<decltype(result.L_newstate)>(sym("luaL_newstate"));
      result.L_openlibs = reinterpret_cast<decltype(result.L_openlibs)>(sym("luaL_openlibs"));
      result.close = reinterpret_cast<decltype(result.close)>(sym("lua_close"));
      result.L_loadbufferx = reinterpret_cast<decltype(result.L_loadbufferx)>(sym("luaL_loadbufferx"));
      result.L_loadfilex = reinterpret_cast<decltype(result.L_loadfilex)>(sym("luaL_loadfilex"));
      result.pcallk = reinterpret_cast<decltype(result.pcallk)>(sym("lua_pcallk"));
      result.pcall51 = reinterpret_cast<decltype(result.pcall51)>(sym("lua_pcall"));
      result.getglobal = reinterpret_cast<decltype(result.getglobal)>(sym("lua_getglobal"));
      result.setglobal = reinterpret_cast<decltype(result.setglobal)>(sym("lua_setglobal"));
      result.getfield = reinterpret_cast<decltype(result.getfield)>(sym("lua_getfield"));
      result.setfield = reinterpret_cast<decltype(result.setfield)>(sym("lua_setfield"));
      result.createtable = reinterpret_cast<decltype(result.createtable)>(sym("lua_createtable"));
      result.pushnumber = reinterpret_cast<decltype(result.pushnumber)>(sym("lua_pushnumber"));
      result.pushstring = reinterpret_cast<decltype(result.pushstring)>(sym("lua_pushstring"));
      result.pushlstring = reinterpret_cast<decltype(result.pushlstring)>(sym("lua_pushlstring"));
      result.pushnil = reinterpret_cast<decltype(result.pushnil)>(sym("lua_pushnil"));
      result.pushlightuserdata = reinterpret_cast<decltype(result.pushlightuserdata)>(sym("lua_pushlightuserdata"));
      result.pushcclosure = reinterpret_cast<decltype(result.pushcclosure)>(sym("lua_pushcclosure"));
      result.touserdata = reinterpret_cast<decltype(result.touserdata)>(sym("lua_touserdata"));
      result.tonumberx = reinterpret_cast<decltype(result.tonumberx)>(sym("lua_tonumberx"));
      result.tonumber51 = reinterpret_cast<decltype(result.tonumber51)>(sym("lua_tonumber"));
      result.isnumber51 = reinterpret_cast<decltype(result.isnumber51)>(sym("lua_isnumber"));
      result.tolstring = reinterpret_cast<decltype(result.tolstring)>(sym("lua_tolstring"));
      result.type = reinterpret_cast<decltype(result.type)>(sym("lua_type"));
      result.settop = reinterpret_cast<decltype(result.settop)>(sym("lua_settop"));
      result.sethook = reinterpret_cast<decltype(result.sethook)>(sym("lua_sethook"));
      result.L_error = reinterpret_cast<decltype(result.L_error)>(sym("luaL_error"));
      result.jit_setmode = reinterpret_cast<decltype(result.jit_setmode)>(sym("luaJIT_setmode"));

      if (result.L_newstate && result.L_openlibs && result.close && result.L_loadbufferx &&
          (result.pcallk || result.pcall51) && result.setfield && result.createtable &&
          result.pushcclosure && result.touserdata && result.tolstring && result.type &&
          result.settop) {
        result.dlHandle = h;
        result.isLua54 = (result.pcallk != nullptr);
        result.registryIndex = result.isLua54 ? -1001000 : -10000;
        result.globalsIndex = -10002;
        result.versionLabel = name;
        result.loaded = true;
        return result;
      }
      ::dlclose(h);
      result = DynamicLuaApi{};
    }
#endif
    return result;
  }();
  return api;
}

void watchdogHook(lua_State* L, void*) {
  if (const DynamicLuaApi* api = apiForState(L); api && api->L_error) {
    api->L_error(L, "script instruction budget exceeded");
  }
}

void captureLuaError(const DynamicLuaApi& api, lua_State* L, const char* prefix, std::string* error) {
  const char* message = api.toString(L, -1);
  if (error) *error = std::string(prefix) + (message ? message : "unknown Lua error");
  api.pop(L, 1);
}

#if NIMBUS_HAS_LUA
const DynamicLuaApi& staticLuaApi() {
  static DynamicLuaApi s = []() {
    DynamicLuaApi r;
    r.loaded = true;
    r.isLua54 = true;
    r.registryIndex = LUA_REGISTRYINDEX;
    r.versionLabel = LUA_RELEASE;
    r.L_newstate = &luaL_newstate;
    r.L_openlibs = &luaL_openlibs;
    r.close = &lua_close;
    r.L_loadbufferx = &luaL_loadbufferx;
    r.L_loadfilex = &luaL_loadfilex;
    r.pcallk = &lua_pcallk;
    r.getglobal = &lua_getglobal;
    r.setglobal = &lua_setglobal;
    r.setfield = &lua_setfield;
    r.createtable = &lua_createtable;
    r.pushnumber = &lua_pushnumber;
    r.pushlightuserdata = &lua_pushlightuserdata;
    r.pushcclosure = &lua_pushcclosure;
    r.touserdata = &lua_touserdata;
    r.tonumberx = &lua_tonumberx;
    r.tolstring = &lua_tolstring;
    r.type = &lua_type;
    r.settop = &lua_settop;
    return r;
  }();
  return s;
}
#endif

const DynamicLuaApi& resolveLuaApi() {
#if NIMBUS_HAS_LUA
  return staticLuaApi();
#else
  return loadDynamicLuaApi();
#endif
}

}  // namespace

Runtime::Runtime() = default;
Runtime::~Runtime() { shutdown(); }

void Runtime::setSandboxEnabled(bool enabled) {
  sandboxed_ = enabled;
  if (state_ && sandboxed_) applySandbox();
}

void Runtime::applySandbox() {
  if (!state_) return;
  const DynamicLuaApi& api = resolveLuaApi();
  if (!api.loaded) return;
  const char* blockedGlobals[] = {"os", "io", "debug", "dofile", "loadfile", "ffi", "jit"};
  for (const char* name : blockedGlobals) {
    if (api.pushnil) api.pushnil(state_);
    api.setGlobal(state_, name);
  }
  // Disable native shared library loading and prevent require('os'|'io'|'debug'|'ffi'|'jit') bypasses
  api.getGlobal(state_, "package");
  if (api.type(state_, -1) == 5 /* LUA_TTABLE */) {
    if (api.pushnil) api.pushnil(state_);
    api.setfield(state_, -2, "loadlib");
    if (api.pushstring) {
      api.pushstring(state_, "");
      api.setfield(state_, -2, "cpath");
    }
    if (api.getfield && api.pushnil) {
      for (const char* pkgSub : {"loaded", "preload"}) {
        api.getfield(state_, -1, pkgSub);
        if (api.type(state_, -1) == 5 /* LUA_TTABLE */) {
          for (const char* mod : {"os", "io", "debug", "ffi", "jit"}) {
            api.pushnil(state_);
            api.setfield(state_, -2, mod);
          }
        }
        api.pop(state_, 1);
      }
    }
  }
  api.pop(state_, 1);
}

void Runtime::armWatchdog() {
  if (!state_ || instructionLimit_ == 0) return;
  const DynamicLuaApi* api = apiForState(state_);
  if (api && api->jit_setmode) {
    // Disable LuaJIT trace compiler so LUA_MASKCOUNT fires reliably inside tight loops.
    api->jit_setmode(state_, 0, 0);
  }
  if (api && api->sethook) {
    api->sethook(state_, &watchdogHook, 8 /* LUA_MASKCOUNT */,
                 static_cast<int>(instructionLimit_));
  }
}

void Runtime::disarmWatchdog() {
  if (!state_) return;
  const DynamicLuaApi* api = apiForState(state_);
  if (api && api->sethook) api->sethook(state_, nullptr, 0, 0);
}

bool Runtime::initialize() {
  if (state_) return true;
  const DynamicLuaApi& api = resolveLuaApi();
  if (!api.loaded) {
    log::write(log::Level::Warning, "script: no Lua 5.4 / LuaJIT runtime found");
    backendName_ = "none";
    return false;
  }

  state_ = api.L_newstate();
  if (!state_) return false;
  {
    std::lock_guard<std::mutex> lock(stateApiMutex());
    stateApiMap()[state_] = &api;
  }
  api.L_openlibs(state_);
  if (sandboxed_) applySandbox();

  api.createtable(state_, 0, 0);
  api.setGlobal(state_, "nimbus");
  backendName_ = api.versionLabel;
  log::write(log::Level::Info, "script: %s initialized (sandbox=%s)", backendName_.c_str(),
             sandboxed_ ? "on" : "off");

  for (auto& [name, function] : plainFunctions_) registerFunction(name, function);
  for (auto& [name, function] : vecFunctions_) registerFunction(name, function);
  for (auto& [name, function] : entityVecFunctions_) registerFunction(name, function);
  for (auto& [name, function] : queryNumberFunctions_) registerFunction(name, function);
  for (auto& [name, function] : queryStringFunctions_) registerFunction(name, function);
  return true;
}

void Runtime::shutdown() {
  if (state_) {
    const DynamicLuaApi* api = apiForState(state_);
    {
      std::lock_guard<std::mutex> lock(stateApiMutex());
      stateApiMap().erase(state_);
    }
    if (api && api->close) api->close(state_);
    state_ = nullptr;
  }
  scripts_.clear();
}

bool Runtime::run(const std::string& source, std::string* error) {
  if (!state_) {
    if (error) *error = "Lua runtime not initialized";
    return false;
  }
  const DynamicLuaApi* api = apiForState(state_);
  if (!api) return false;
  if (api->L_loadbufferx(state_, source.c_str(), source.size(), "chunk", nullptr) != 0) {
    captureLuaError(*api, state_, "Lua load: ", error);
    return false;
  }
  armWatchdog();
  const int rc = api->call(state_, 0, 0);
  disarmWatchdog();
  if (rc != 0) {
    captureLuaError(*api, state_, "Lua run: ", error);
    return false;
  }
  return true;
}

bool Runtime::runFile(const std::string& path, std::string* error) {
  if (!state_) {
    if (error) *error = "Lua runtime not initialized";
    return false;
  }
  const DynamicLuaApi* api = apiForState(state_);
  if (!api || !api->L_loadfilex) return false;
  if (api->L_loadfilex(state_, path.c_str(), nullptr) != 0) {
    captureLuaError(*api, state_, "Lua load file: ", error);
    return false;
  }
  armWatchdog();
  const int rc = api->call(state_, 0, 0);
  disarmWatchdog();
  if (rc != 0) {
    captureLuaError(*api, state_, "Lua run file: ", error);
    return false;
  }
  return true;
}

bool Runtime::call(const std::string& function, const std::vector<double>& args,
                   double* result, std::string* error) {
  if (!state_) {
    if (error) *error = "Lua runtime not initialized";
    return false;
  }
  const DynamicLuaApi* api = apiForState(state_);
  if (!api) return false;
  api->getGlobal(state_, function.c_str());
  if (api->type(state_, -1) != 6 /* LUA_TFUNCTION */) {
    api->pop(state_, 1);
    if (error) *error = "Lua global '" + function + "' is not a function";
    return false;
  }
  for (double arg : args) api->pushnumber(state_, arg);
  armWatchdog();
  const int rc = api->call(state_, static_cast<int>(args.size()), 1);
  disarmWatchdog();
  if (rc != 0) {
    captureLuaError(*api, state_, "Lua call: ", error);
    return false;
  }
  if (result && api->isNumber(state_, -1)) *result = api->toNumber(state_, -1);
  api->pop(state_, 1);
  return true;
}

void Runtime::registerFunction(const std::string& name, std::function<void()> fn) {
  plainFunctions_[name] = std::move(fn);
  if (!state_) return;
  const DynamicLuaApi* api = apiForState(state_);
  if (!api) return;
  api->getGlobal(state_, "nimbus");
  api->pushlightuserdata(state_, &plainFunctions_[name]);
  api->pushcclosure(state_, [](lua_State* L) -> int {
    const DynamicLuaApi* a = apiForState(L);
    if (!a) return 0;
    auto* callback = static_cast<std::function<void()>*>(a->touserdata(L, a->upvalueIndex(1)));
    if (callback && *callback) (*callback)();
    return 0;
  }, 1);
  api->setfield(state_, -2, name.c_str());
  api->pop(state_, 1);
}

void Runtime::registerFunction(const std::string& name,
                               std::function<void(double, double, double)> fn) {
  vecFunctions_[name] = std::move(fn);
  if (!state_) return;
  const DynamicLuaApi* api = apiForState(state_);
  if (!api) return;
  api->getGlobal(state_, "nimbus");
  api->pushlightuserdata(state_, &vecFunctions_[name]);
  api->pushcclosure(state_, [](lua_State* L) -> int {
    const DynamicLuaApi* a = apiForState(L);
    if (!a) return 0;
    auto* callback = static_cast<std::function<void(double, double, double)>*>(
        a->touserdata(L, a->upvalueIndex(1)));
    if (callback && *callback)
      (*callback)(a->toNumber(L, 1), a->toNumber(L, 2), a->toNumber(L, 3));
    return 0;
  }, 1);
  api->setfield(state_, -2, name.c_str());
  api->pop(state_, 1);
}

void Runtime::registerFunction(
    const std::string& name,
    std::function<void(const std::string&, double, double, double)> fn) {
  entityVecFunctions_[name] = std::move(fn);
  if (!state_) return;
  const DynamicLuaApi* api = apiForState(state_);
  if (!api) return;
  api->getGlobal(state_, "nimbus");
  api->pushlightuserdata(state_, &entityVecFunctions_[name]);
  api->pushcclosure(state_, [](lua_State* L) -> int {
    const DynamicLuaApi* a = apiForState(L);
    if (!a) return 0;
    auto* callback = static_cast<
        std::function<void(const std::string&, double, double, double)>*>(
        a->touserdata(L, a->upvalueIndex(1)));
    const char* entity = a->toString(L, 1);
    if (callback && *callback && entity) {
      (*callback)(entity, a->toNumber(L, 2), a->toNumber(L, 3), a->toNumber(L, 4));
    }
    return 0;
  }, 1);
  api->setfield(state_, -2, name.c_str());
  api->pop(state_, 1);
}

void Runtime::registerFunction(const std::string& name,
                               std::function<double(const std::string&)> fn) {
  queryNumberFunctions_[name] = std::move(fn);
  if (!state_) return;
  const DynamicLuaApi* api = apiForState(state_);
  if (!api) return;
  api->getGlobal(state_, "nimbus");
  api->pushlightuserdata(state_, &queryNumberFunctions_[name]);
  api->pushcclosure(state_, [](lua_State* L) -> int {
    const DynamicLuaApi* a = apiForState(L);
    if (!a) return 0;
    auto* callback = static_cast<std::function<double(const std::string&)>*>(
        a->touserdata(L, a->upvalueIndex(1)));
    const char* arg = a->toString(L, 1);
    const double val = (callback && *callback && arg) ? (*callback)(arg) : 0.0;
    a->pushnumber(L, val);
    return 1;
  }, 1);
  api->setfield(state_, -2, name.c_str());
  api->pop(state_, 1);
}

void Runtime::registerFunction(const std::string& name,
                               std::function<std::string(const std::string&)> fn) {
  queryStringFunctions_[name] = std::move(fn);
  if (!state_) return;
  const DynamicLuaApi* api = apiForState(state_);
  if (!api) return;
  api->getGlobal(state_, "nimbus");
  api->pushlightuserdata(state_, &queryStringFunctions_[name]);
  api->pushcclosure(state_, [](lua_State* L) -> int {
    const DynamicLuaApi* a = apiForState(L);
    if (!a) return 0;
    auto* callback = static_cast<std::function<std::string(const std::string&)>*>(
        a->touserdata(L, a->upvalueIndex(1)));
    const char* arg = a->toString(L, 1);
    const std::string res = (callback && *callback && arg) ? (*callback)(arg) : std::string{};
    if (a->pushlstring) a->pushlstring(L, res.data(), res.size());
    else if (a->pushstring) a->pushstring(L, res.c_str());
    return 1;
  }, 1);
  api->setfield(state_, -2, name.c_str());
  api->pop(state_, 1);
}

void Runtime::attach(ecs::Entity entity, const std::string& source) {
  Script entry;
  entry.entity = entity;
  entry.source = source;
  scripts_[entity] = entry;
  std::string error;
  if (!run(source, &error)) {
    scripts_[entity].loaded = false;
    log::write(log::Level::Error, "script: attach entity %llu failed: %s",
               static_cast<unsigned long long>(entity), error.c_str());
    return;
  }
  scripts_[entity].loaded = true;
}

void Runtime::detach(ecs::Entity entity) { scripts_.erase(entity); }

bool Runtime::reload(ecs::Entity entity, const std::string& source, std::string* error) {
  auto it = scripts_.find(entity);
  if (it == scripts_.end()) {
    if (error) *error = "entity has no attached script";
    return false;
  }
  if (!run(source, error)) return false;
  it->second.source = source;
  it->second.loaded = true;
  return true;
}

void Runtime::update(float deltaSeconds) {
  if (!state_) return;
  const DynamicLuaApi* api = apiForState(state_);
  if (!api) return;
  for (auto& [entity, script] : scripts_) {
    if (!script.loaded) continue;
    api->getGlobal(state_, "update");
    if (api->type(state_, -1) != 6 /* LUA_TFUNCTION */) {
      api->pop(state_, 1);
      continue;
    }
    api->pushnumber(state_, deltaSeconds);
    api->pushnumber(state_, static_cast<double>(entity));
    armWatchdog();
    const int rc = api->call(state_, 2, 0);
    disarmWatchdog();
    if (rc != 0) {
      const char* message = api->toString(state_, -1);
      log::write(log::Level::Error, "script: update entity %llu failed: %s",
                 static_cast<unsigned long long>(entity), message ? message : "?");
      api->pop(state_, 1);
      script.loaded = false;
    }
  }
}

}  // namespace nimbus::script
