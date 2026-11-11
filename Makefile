SOURCES += $(wildcard src/*.cpp)
DISTRIBUTABLES += presets res $(wildcard LICENSE*)

RACK_DIR ?= C:/vcv/Rack-SDK
include $(RACK_DIR)/plugin.mk
