// Nimbus script module: dynamic LuaJIT 5.1 / Lua 5.4 runtime, sandbox, reflection, coroutines.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "nimbus/ecs.hpp"

typedef struct lua_State lua_State;

namespace nimbus::script {

class Runtime {
 public:
  Runtime();
  ~Runtime();

  bool initialize();
  void shutdown();
  bool valid() const { return state_ != nullptr; }

  // Security & budget controls. Call before or after initialize().
  void setSandboxEnabled(bool enabled);
  bool sandboxEnabled() const { return sandboxed_; }
  void setInstructionLimit(uint32_t maxInstructions) { instructionLimit_ = maxInstructions; }
  uint32_t instructionLimit() const { return instructionLimit_; }

  bool run(const std::string& source, std::string* error = nullptr);
  bool runFile(const std::string& path, std::string* error = nullptr);

  bool call(const std::string& function, const std::vector<double>& args = {},
            double* result = nullptr, std::string* error = nullptr);

  // Registers C++ functions into the `nimbus` Lua table.
  void registerFunction(const std::string& name, std::function<void()> fn);
  void registerFunction(const std::string& name,
                        std::function<void(double, double, double)> fn);
  void registerFunction(const std::string& name,
                        std::function<void(const std::string&, double, double, double)> fn);
  void registerFunction(const std::string& name,
                        std::function<double(const std::string&)> fn);
  void registerFunction(const std::string& name,
                        std::function<std::string(const std::string&)> fn);

  void attach(ecs::Entity entity, const std::string& script);
  void detach(ecs::Entity entity);
  void update(float deltaSeconds);
  uint32_t scriptCount() const { return static_cast<uint32_t>(scripts_.size()); }

  bool reload(ecs::Entity entity, const std::string& source, std::string* error = nullptr);

  lua_State* state() { return state_; }
  const std::string& backendName() const { return backendName_; }

 private:
  void applySandbox();
  void armWatchdog();
  void disarmWatchdog();

  lua_State* state_ = nullptr;
  bool sandboxed_ = true;
  uint32_t instructionLimit_ = 1000000u;
  std::string backendName_ = "none";

  struct Script {
    ecs::Entity entity = ecs::kNullEntity;
    std::string source;
    std::string path;
    bool loaded = false;
  };
  std::unordered_map<ecs::Entity, Script> scripts_;
  std::unordered_map<std::string, std::function<void()>> plainFunctions_;
  std::unordered_map<std::string, std::function<void(double, double, double)>> vecFunctions_;
  std::unordered_map<std::string,
      std::function<void(const std::string&, double, double, double)>> entityVecFunctions_;
  std::unordered_map<std::string, std::function<double(const std::string&)>> queryNumberFunctions_;
  std::unordered_map<std::string, std::function<std::string(const std::string&)>>
      queryStringFunctions_;
};

}  // namespace nimbus::script
