# Copyright (c) 2020 Mark Polyakov, Karen Haining, Muki Kiboigo, Lucius Carr
# (If you edit the file, add your name here!)
#
# Permission is hereby granted, free of charge, ...

# ------------------------------------------------------
# Source discovery
# ------------------------------------------------------

SRCS := $(wildcard src/*.cpp)
TESTS := $(wildcard test/*.cpp)

SUBSYS_LIVE_SRCS := $(wildcard livedebug/*.cpp)
SUBSYS_LIVE_OBJS := $(patsubst %.cpp,%.o,$(SUBSYS_LIVE_SRCS))
SUBSYS_LIVE_DEPS := $(patsubst %.cpp,%.d,$(SUBSYS_LIVE_SRCS))
SUBSYS_LIVE_BIN  := lost-livedebug

MANS := $(wildcard documentation/*.man)
MAN_TXTS := $(patsubst documentation/%.man, documentation/%.txt, $(MANS))
MAN_HS := $(patsubst documentation/%.man, documentation/man-%.h, $(MANS))
DOXYGEN_DIR := ./documentation/doxygen

OBJS := $(patsubst %.cpp,%.o,$(SRCS))
TEST_OBJS := $(patsubst %.cpp,%.o,$(TESTS) $(filter-out %/main.o, $(OBJS)))

DEPS := $(patsubst %.cpp,%.d,$(SRCS) $(TESTS) $(SUBSYS_LIVE_SRCS))

BIN     := lost
TEST_BIN := ./lost-test
BSC     := bright-star-catalog.tsv

# ------------------------------------------------------
# Libraries and compiler flags
# ------------------------------------------------------

LIBS     := -lcairo
SFML_LIBS := -lsfml-graphics -lsfml-window -lsfml-system
CXXFLAGS := $(CXXFLAGS) -Ivendor -Isrc -Idocumentation -Wall -Wextra -Wno-missing-field-initializers -pedantic --std=c++11

# --- macOS/Homebrew Path Auto-Detection ---
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # Check if Homebrew is in the Apple Silicon path (/opt/homebrew) or Intel path (/usr/local)
    HOMEBREW_PREFIX := $(shell [ -d /opt/homebrew ] && echo /opt/homebrew || echo /usr/local)
    
    # Add Homebrew to the include and library search paths
    CXXFLAGS += -I$(HOMEBREW_PREFIX)/include
    LDFLAGS  += -L$(HOMEBREW_PREFIX)/lib
endif

RELEASE_CXXFLAGS := $(CXXFLAGS) -O3
CXXFLAGS := $(CXXFLAGS) -ggdb -fno-omit-frame-pointer

ifndef LOST_DISABLE_ASAN
    CXXFLAGS := $(CXXFLAGS) -fsanitize=address
    LDFLAGS  := $(LDFLAGS) -fsanitize=address
endif

RELEASE_LDFLAGS := $(LDFLAGS)

ifdef LOST_FLOAT_MODE
    CXXFLAGS := $(CXXFLAGS) -Wdouble-promotion -Werror=double-promotion -D LOST_FLOAT_MODE
endif

# ------------------------------------------------------
# Primary build rules
# ------------------------------------------------------

all: $(BIN) $(BSC)

release: CXXFLAGS := $(RELEASE_CXXFLAGS)
release: LDFLAGS := $(RELEASE_LDFLAGS)
release: all

# Main program -----------------------------------------
$(BIN): $(OBJS)
	$(CXX) $(LDFLAGS) -o $(BIN) $(OBJS) $(LIBS)

# NEW: Live Debug Program -------------------------------
# Builds: lost-livedebug
$(SUBSYS_LIVE_BIN): $(SUBSYS_LIVE_OBJS) $(filter-out src/main.o, $(OBJS))
	$(CXX) $(CXXFLAGS) -o $@ $^ $(SFML_LIBS) $(LIBS) $(LDFLAGS)

livedebug: $(SUBSYS_LIVE_BIN)

# Manpage conversion ------------------------------------
documentation/%.txt: documentation/%.man
	groff -mandoc -Tascii $< > $@
	printf '\0' >> $@

documentation/man-%.h: documentation/%.txt
	xxd -i $< > $@

src/main.o: $(MAN_HS)

docs:
	doxygen

lint:
	cpplint --recursive src test

# Object compilation rule -------------------------------
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

# Include all auto-deps
-include $(DEPS)

# Testing ------------------------------------------------
test: $(BIN) $(BSC) $(TEST_BIN)
	$(TEST_BIN)
	bash ./test/scripts/readme-examples-test.sh
	bash ./test/scripts/random-crap.sh

$(TEST_BIN): $(TEST_OBJS)
	$(CXX) $(LDFLAGS) -o $(TEST_BIN) $(TEST_OBJS) $(LIBS)

# Cleaning ----------------------------------------------
clean:
	rm -f $(OBJS) $(DEPS) $(TEST_OBJS) $(SUBSYS_LIVE_OBJS) $(MAN_HS)
	rm -rf $(DOXYGEN_DIR)

clean_all: clean
	rm -f $(BSC) $(SUBSYS_LIVE_BIN)

.PHONY: all clean test docs lint livedebug release