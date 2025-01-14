# If RACK_DIR is not defined when calling the Makefile, default to two directories above
RACK_DIR ?= ../..

DAISYSP_DIR = ./thirdparty/DaisySP
DAISYSP_SOURCE_DIR = $(DAISYSP_DIR)/Source
DAISYSP_LGPL_DIR = ./thirdparty/DaisySP-LGPL

DEEPNOTE_DIR = ./thirdparty/DeepNote

# FLAGS will be passed to both the C and C++ compiler
FLAGS +=
CFLAGS +=
CXXFLAGS += -I$(DAISYSP_SOURCE_DIR) -I$(DAISYSP_SOURCE_DIR)/Utility -I$(DEEPNOTE_DIR)/src

# Add .cpp files to the build
SOURCES += $(wildcard src/*.cpp)
SOURCES += $(DAISYSP_DIR)/Source/Synthesis/oscillator.cpp

# Add files to the ZIP package when running `make dist`
# The compiled plugin and "plugin.json" are automatically added.
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += $(wildcard presets)

# Include the Rack plugin Makefile framework
include $(RACK_DIR)/plugin.mk
