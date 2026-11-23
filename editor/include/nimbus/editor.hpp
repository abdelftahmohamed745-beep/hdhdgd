// Nimbus editor: scene tree, undo/redo command history, inspector, visual scripting, project export.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "nimbus/engine.hpp"
#include "nimbus/log.hpp"

namespace nimbus::editor {

struct LogEntry {
  log::Level level = log::Level::Info;
  std::string text;
  double time = 0.0;
};

struct UndoCommand {
  std::string label;
  std::function<void()> undo;
  std::function<void()> redo;
};

class CommandHistory {
 public:
  void execute(UndoCommand cmd);
  void record(UndoCommand cmd);  // Records an already-performed action without calling redo().
  bool undo();
  bool redo();
  bool canUndo() const { return cursor_ > 0; }
  bool canRedo() const { return cursor_ < commands_.size(); }
  uint32_t undoCount() const { return static_cast<uint32_t>(cursor_); }
  uint32_t redoCount() const { return static_cast<uint32_t>(commands_.size() - cursor_); }
  void clear() {
    commands_.clear();
    cursor_ = 0;
  }

 private:
  static constexpr size_t kMaxHistory = 128;
  std::vector<UndoCommand> commands_;
  size_t cursor_ = 0;
};

// A visual scripting graph: nodes with typed pins and edges.
struct VsPin {
  std::string name;
  enum class Type : uint8_t { Exec, Float, Vector3, Bool, Entity } type = Type::Float;
  bool input = true;
};

struct VsNode {
  uint32_t id = 0;
  std::string type;
  std::string name;
  std::vector<VsPin> inputs;
  std::vector<VsPin> outputs;
  float x = 0.0f, y = 0.0f;
  std::string expression;
};

struct VsEdge {
  uint32_t fromNode = 0;
  uint32_t fromPin = 0;
  uint32_t toNode = 0;
  uint32_t toPin = 0;
};

struct VsExecutionContext {
  std::unordered_map<std::string, double> numbers;
  std::unordered_map<std::string, ecs::Entity> entities;
  std::unordered_map<std::string, math::Vec3> vectors;
  std::function<void(double)> print;
  std::function<void(ecs::Entity, const math::Vec3&)> setPosition;
  std::function<math::Vec3(ecs::Entity)> getPosition;
  std::function<void(ecs::Entity, const math::Vec3&)> applyForce;
  std::function<void(double)> playSound;
  std::function<void(const math::Vec3&)> spawnParticle;
  std::vector<uint32_t>* trace = nullptr;
};

class VisualScript {
 public:
  uint32_t addNode(const std::string& type, const std::string& name, float x, float y);
  void removeNode(uint32_t id);
  bool addEdge(const VsEdge& edge);
  void removeEdge(uint32_t fromNode, uint32_t fromPin, uint32_t toNode, uint32_t toPin);
  void clear();

  double evaluate(uint32_t entry, const std::unordered_map<std::string, double>& inputs) const;
  bool evaluateBool(uint32_t entry, const std::unordered_map<std::string, double>& inputs) const;
  bool execute(uint32_t entry, const VsExecutionContext& context,
               uint32_t maxExecutedNodes = 1024) const;

  std::string serializeJson() const;
  bool deserializeJson(const std::string& json);
  std::string toLua() const;

  const std::vector<VsNode>& nodes() const { return nodes_; }
  const std::vector<VsEdge>& edges() const { return edges_; }

 private:
  std::vector<VsNode> nodes_;
  std::vector<VsEdge> edges_;
  uint32_t nextId_ = 1;
};

// The editor holds the engine, selection state, project state, and undo history.
class Editor {
 public:
  explicit Editor(Engine* engine) : engine_(engine) {}

  bool newProject(const std::string& path, const std::string& name,
                  const std::string& templateName = "empty");
  bool openProject(const std::string& path);
  bool saveProject(const std::string& path);
  bool exportProject(const std::string& destPath);
  std::string projectPath() const { return projectPath_; }
  std::string projectName() const { return projectName_; }

  // Entity selection and inspector access.
  void select(ecs::Entity entity) { selected_ = entity; }
  ecs::Entity selected() const { return selected_; }
  std::vector<ecs::Entity> entities() const;
  std::string entitySummary(ecs::Entity entity) const;

  // Transform & hierarchy editing with undo/redo support.
  void setPosition(ecs::Entity entity, const math::Vec3& position, bool recordUndo = true);
  void setTransform(ecs::Entity entity, const scene::Transform& transform, bool recordUndo = true);
  math::Vec3 getPosition(ecs::Entity entity) const;
  bool undo() { return history_.undo(); }
  bool redo() { return history_.redo(); }
  CommandHistory* history() { return &history_; }
  const CommandHistory* history() const { return &history_; }

  // Visual scripting.
  VisualScript* visualScript() { return &visualScript_; }

  // Logs captured for the console panel.
  std::vector<LogEntry> logs(uint32_t offset, uint32_t limit) const;
  uint32_t logTotal() const;

  // Playback control.
  void play();
  void pause();
  void stop();
  void stepFrame();

  Engine* engine() { return engine_; }

 private:
  Engine* engine_;
  std::string projectPath_;
  std::string projectName_;
  ecs::Entity selected_ = ecs::kNullEntity;
  VisualScript visualScript_;
  CommandHistory history_;
};

}  // namespace nimbus::editor
