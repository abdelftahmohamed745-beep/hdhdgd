// Nimbus MCP server entry point: engine + editor + MCP over stdio.
#include <cstdio>
#include <memory>
#include <string>

#include "nimbus/editor.hpp"
#include "nimbus/engine.hpp"
#include "nimbus/log.hpp"
#include "nimbus/mcp.hpp"

int main(int argc, char** argv) {
  nimbus::log::setLevel(nimbus::log::Level::Info);
  auto engine = std::make_unique<nimbus::Engine>();
  if (!engine->initialize("nimbus-mcp", 1280, 720, nimbus::rhi::Api::Headless)) {
    std::fprintf(stderr, "nimbus-mcp: engine init failed\n");
    return 1;
  }
  nimbus::editor::Editor editor(engine.get());
  nimbus::mcp::Server server(&editor);
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--workspace") {
      server.setWorkspaceRoot(argv[i + 1]);
    }
  }
  server.registerTools();
  std::fprintf(stderr, "nimbus-mcp: %u tools registered\n", server.toolCount());
  server.runStdio();
  engine->shutdown();
  return 0;
}
