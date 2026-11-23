// Nimbus MCP server: AI-facing tools over a live engine.
// Implements the Model Context Protocol stdio transport (JSON-RPC 2.0).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "nimbus/editor.hpp"

namespace nimbus::mcp {

struct Call {
  std::string name;
  std::string argumentsJson;
};

struct Result {
  bool ok = true;
  std::string json;
  std::string error;

  Result() = default;
  Result(bool success, std::string payload, std::string message = {})
      : ok(success), json(std::move(payload)), error(std::move(message)) {}
};

class Server {
 public:
  explicit Server(editor::Editor* editor) : editor_(editor) {}

  void setWorkspaceRoot(const std::string& rootPath);
  const std::string& workspaceRoot() const { return workspaceRoot_; }

  void registerTools();
  std::string handle(const std::string& request);
  void runStdio();

  uint32_t toolCount() const { return static_cast<uint32_t>(tools_.size()); }
  const std::vector<std::string>& toolNames() const { return names_; }

  int runTests();
  Result invoke(const std::string& name, const std::string& arguments);

 private:
  using Handler = std::function<Result(const std::string&)>;
  void addTool(const std::string& name, const std::string& description,
               const std::string& inputSchemaJson, Handler handler);
  bool validateAndResolvePath(const std::string& rawPath, std::string& outResolved,
                              std::string& outError) const;

  editor::Editor* editor_;
  std::string workspaceRoot_;
  std::vector<std::string> names_;
  std::vector<std::string> descriptions_;
  std::vector<std::string> schemas_;
  std::unordered_map<std::string, Handler> tools_;
};

}  // namespace nimbus::mcp
