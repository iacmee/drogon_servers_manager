CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -O2 -g
CXXFLAGS += -Iinclude -Imodels
CXXFLAGS += $(shell pkg-config --cflags jsoncpp) 
CXXFLAGS += $(shell pkg-config --cflags libsodium)
CXXFLAGS += $(shell pkg-config --cflags libsystemd)

TARGET := servers_manager

SOURCES := $(shell find src -type f -name '*.cpp')
MODEL_SOURCES := $(shell find models -type f -name '*.cc')
OBJ_DIR := obj/
OBJECTS := $(SOURCES:src/%.cpp=obj/%.o)
OBJECTS += $(MODEL_SOURCES:models/%.cc=obj/models/%.o)
LIBS := -L/usr/local/lib -ldrogon -ltrantor $(shell pkg-config --libs jsoncpp)
LIBS += $(shell pkg-config --libs sqlite3)
LIBS += $(shell pkg-config --libs libsodium)
LIBS += $(shell pkg-config --libs libsystemd)

.PHONY: all run cert clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LIBS)

$(OBJ_DIR)%.o: src/%.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)models/%.o: models/%.cc
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run r: all
	./$(TARGET) config/config.json

re: clean all

#questa regola sarà da togliere
reset_db:
	rm db/database.db
	touch db/database.db

clean:
	rm -rf $(OBJ_DIR)
	rm -rf $(TARGET)

