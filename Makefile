# Nimbus Engine — Portable Makefile for GCC/Clang C++20 builds
CXX ?= g++
BUILD ?= debug
SANITIZE ?= none
BUILD_DIR ?= build/$(BUILD)-$(SANITIZE)

COMMON_FLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Werror -MMD -MP -pthread
INCLUDES := \
	-Iengine/core/include \
	-Iengine/input/include \
	-Iengine/ecs/include \
	-Iengine/scene/include \
	-Iengine/rhi/include \
	-Iengine/render/include \
	-Iengine/physics/include \
	-Iengine/anim/include \
	-Iengine/terrain/include \
	-Iengine/particles/include \
	-Iengine/audio/include \
	-Iengine/ai/include \
	-Iengine/net/include \
	-Iengine/ui/include \
	-Iengine/asset/include \
	-Iengine/script/include \
	-Iengine/engine/include \
	-Ieditor/include \
	-Ieditor/mcp/include

ifeq ($(BUILD),release)
	OPT_FLAGS := -O2 -DNDEBUG
else
	OPT_FLAGS := -O1 -g
endif

SAN_FLAGS :=
ifeq ($(SANITIZE),asan)
	SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
endif
ifeq ($(SANITIZE),tsan)
	SAN_FLAGS := -fsanitize=thread -fno-omit-frame-pointer
endif

CXXFLAGS := $(COMMON_FLAGS) $(OPT_FLAGS) $(SAN_FLAGS) $(INCLUDES) -DNIMBUS_HAS_DYNAMIC_LUA=1
LDFLAGS := -pthread -ldl $(SAN_FLAGS)

ENGINE_SRCS := $(wildcard \
	engine/core/src/*.cpp \
	engine/input/src/*.cpp \
	engine/scene/src/*.cpp \
	engine/rhi/src/*.cpp \
	engine/render/src/*.cpp \
	engine/physics/src/*.cpp \
	engine/anim/src/*.cpp \
	engine/terrain/src/*.cpp \
	engine/particles/src/*.cpp \
	engine/audio/src/*.cpp \
	engine/ai/src/*.cpp \
	engine/net/src/*.cpp \
	engine/ui/src/*.cpp \
	engine/asset/src/*.cpp \
	engine/script/src/*.cpp \
	engine/engine/src/*.cpp)

FILTERED_ENGINE_SRCS := $(filter-out engine/rhi/src/vulkan_device.cpp engine/rhi/src/gles_device.cpp,$(ENGINE_SRCS))
EDITOR_SRCS := $(wildcard editor/src/*.cpp editor/mcp/src/*.cpp)

CORE_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(FILTERED_ENGINE_SRCS) $(EDITOR_SRCS))
TEST_OBJ := $(BUILD_DIR)/tests/main.o
MCP_OBJ := $(BUILD_DIR)/editor/mcp/main.o
EX_FPS_OBJ := $(BUILD_DIR)/examples/fps_arena_3d/main.o
EX_AI_OBJ := $(BUILD_DIR)/examples/ai_patrol_sim/main.o
DEPS := $(CORE_OBJS:.o=.d) $(TEST_OBJ:.o=.d) $(MCP_OBJ:.o=.d) $(EX_FPS_OBJ:.o=.d) $(EX_AI_OBJ:.o=.d)

.PHONY: all test mcp examples clean

all: $(BUILD_DIR)/nimbus_tests $(BUILD_DIR)/nimbus_mcp_server $(BUILD_DIR)/nimbus_example_fps_arena $(BUILD_DIR)/nimbus_example_ai_patrol

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/nimbus_tests: $(CORE_OBJS) $(TEST_OBJ)
	$(CXX) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/nimbus_mcp_server: $(CORE_OBJS) $(MCP_OBJ)
	$(CXX) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/nimbus_example_fps_arena: $(CORE_OBJS) $(EX_FPS_OBJ)
	$(CXX) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/nimbus_example_ai_patrol: $(CORE_OBJS) $(EX_AI_OBJ)
	$(CXX) $^ $(LDFLAGS) -o $@

test: $(BUILD_DIR)/nimbus_tests
	./$(BUILD_DIR)/nimbus_tests

mcp: $(BUILD_DIR)/nimbus_mcp_server

examples: $(BUILD_DIR)/nimbus_example_fps_arena $(BUILD_DIR)/nimbus_example_ai_patrol
	./$(BUILD_DIR)/nimbus_example_fps_arena
	./$(BUILD_DIR)/nimbus_example_ai_patrol

clean:
	rm -rf build

-include $(DEPS)
