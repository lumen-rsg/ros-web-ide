# ============================================================
# ROS Web IDE Backend — Makefile
# ============================================================

# --- Toolchain ---
CXX       := g++
CXXFLAGS  := -std=c++23 -Wall -Wextra -Wpedantic -fPIC

# --- Build type ---
BUILD_TYPE ?= debug
ifeq ($(BUILD_TYPE),release)
    CXXFLAGS += -O2 -DNDEBUG
else
    CXXFLAGS += -g -O0 -DDEBUG
endif

# --- Platform detection ---
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    PLATFORM_FLAGS := -DLINUX
    LDFLAGS        := -lpthread -lutil
endif
ifeq ($(UNAME_S),Darwin)
    PLATFORM_FLAGS := -DMACOS
    LDFLAGS        := -lpthread -framework CoreServices
endif

CXXFLAGS += $(PLATFORM_FLAGS)

# --- Directory layout ---
SRC_DIR      := src
BUILD_DIR    := build/$(BUILD_TYPE)
THIRD_DIR    := third_party
TEST_DIR     := tests

# --- Include paths ---
CROW_INC     := -I $(THIRD_DIR)/crow/include
ASIO_INC     := -I $(THIRD_DIR)/asio/asio/include
JSON_INC     := -I $(THIRD_DIR)/nlohmann/single_include
DOCTEST_INC  := -I $(THIRD_DIR)/doctest/doctest
SRC_INC      := -I $(SRC_DIR)

INCLUDES := $(SRC_INC) $(CROW_INC) $(ASIO_INC) $(JSON_INC)

# --- Source file discovery ---
LIB_SRCS     := $(filter-out $(SRC_DIR)/main.cpp, $(shell find $(SRC_DIR) -name '*.cpp'))
MAIN_SRC     := $(SRC_DIR)/main.cpp
TEST_SRCS    := $(shell find $(TEST_DIR) -name '*.cpp')

# --- Object file mapping ---
LIB_OBJS     := $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(LIB_SRCS))
MAIN_OBJ     := $(BUILD_DIR)/main.o
TEST_OBJS    := $(patsubst $(TEST_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(TEST_SRCS))

# --- Target binaries ---
TARGET      := $(BUILD_DIR)/ros-web-ide
TEST_TARGET := $(BUILD_DIR)/run_tests

# ============================================================
# Phony targets
# ============================================================
.PHONY: all clean test debug release run

all: $(TARGET)

debug:
	$(MAKE) BUILD_TYPE=debug all

release:
	$(MAKE) BUILD_TYPE=release all

run: $(TARGET)
	./$(TARGET)

# ============================================================
# Build the application
# ============================================================
$(TARGET): $(MAIN_OBJ) $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# ============================================================
# Build the test runner
# ============================================================
test: $(TEST_TARGET)
	./$(TEST_TARGET) --duration

$(TEST_TARGET): $(TEST_OBJS) $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# ============================================================
# Compile rules
# ============================================================
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(DOCTEST_INC) -c $< -o $@

# ============================================================
# Clean
# ============================================================
clean:
	rm -rf build/
