# The production build uses the locally patched, pinned Drogon dependency.
DROGON_PREFIX := $(CURDIR)/.deps/install
ifeq ($(wildcard $(DROGON_PREFIX)/paperpilot-patched),)
$(error Run bash tools/build_drogon.sh /path/to/drogon-checkout first)
endif

CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -O2
CXXFLAGS += -I$(DROGON_PREFIX)/include -Iinclude -Imodels
CXXFLAGS += $(shell pkg-config --cflags jsoncpp) 
CXXFLAGS += $(shell pkg-config --cflags libsodium)
CXXFLAGS += $(shell pkg-config --cflags libsystemd)

TARGET := servers_manager

SOURCES := $(shell find src -type f -name '*.cpp')
MODEL_SOURCES := $(shell find models -type f -name '*.cc')
OBJ_DIR := obj/
OBJECTS := $(SOURCES:src/%.cpp=obj/%.o)
OBJECTS += $(MODEL_SOURCES:models/%.cc=obj/models/%.o)
DEPFILES := $(OBJECTS:.o=.d)
LIBS := -L$(DROGON_PREFIX)/lib -Wl,-rpath,'$$ORIGIN/.deps/install/lib:$$ORIGIN/../.deps/install/lib' -ldrogon -ltrantor $(shell pkg-config --libs jsoncpp)
LIBS += $(shell pkg-config --libs sqlite3)
LIBS += $(shell pkg-config --libs libsodium)
LIBS += $(shell pkg-config --libs libsystemd)

.PHONY: all run cert clean

all: $(TARGET)

$(TARGET): $(OBJECTS) $(DROGON_PREFIX)/paperpilot-patched
	$(CXX) $(OBJECTS) -o $@ $(LIBS)

$(OBJ_DIR)%.o: src/%.cpp $(DROGON_PREFIX)/paperpilot-patched
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)models/%.o: models/%.cc $(DROGON_PREFIX)/paperpilot-patched
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPFILES)

run r: all
	./$(TARGET) config/config.json

re: clean all

#questa regola sarà da togliere
reset_db:
	rm -f db/database.db
	mkdir -p db/ && touch db/database.db

clean:
	rm -rf $(OBJ_DIR)
	rm -rf $(TARGET)

.PHONY: test-security
obj/security_test_server: tests/security_server.cpp $(filter-out obj/main.o,$(OBJECTS))
	$(CXX) $(CXXFLAGS) $< $(filter-out obj/main.o,$(OBJECTS)) -o $@ $(LIBS)

test-security: obj/security_test_server
	python3 tests/security_websocket.py obj/security_test_server

.PHONY: test-console
test-console: obj/MinecraftConsole.o obj/ConsoleOutput.o obj/SessionAccess.o
	$(CXX) $(CXXFLAGS) tests/console_limits.cpp $^ -o obj/console_limits_test $(LIBS)
	./obj/console_limits_test

obj/auth_unit_test: tests/auth_security.cpp obj/PasswordExecutor.o obj/RegistrationFilter.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBS)

.PHONY: test-auth
test-auth: obj/auth_unit_test obj/security_test_server
	./obj/auth_unit_test
	python3 tests/auth_http.py obj/security_test_server
