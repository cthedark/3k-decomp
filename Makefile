# Build the native 3K slice. Needs a C99 compiler, a C++14 compiler (for the vendored ymfm OPL2
# emulator, third_party/ymfm) and SDL2 (sdl2-config on PATH).
#
#   make            build build/3k
#   make test       build and run the headless tests
#   make clean      remove build/
#
# The game data files are NOT part of this repository. Point the program at your
# own copy: build/3k --data /path/to/files   (or set TK_DATA).

CC      ?= cc
CXX     ?= c++
CFLAGS  ?= -std=c99 -O2 -g -Wall -Wextra -Wpedantic
# ymfm is third-party code: built without -Wextra/-Wpedantic so its warnings stay out of ours.
CXXFLAGS ?= -std=c++14 -O2 -g -Wall
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)

# C programs that contain C++ objects are linked by the C driver, so the C++ runtime is named.
ifeq ($(shell uname -s),Darwin)
CXXLIB := -lc++
else
CXXLIB := -lstdc++
endif

BUILD := build

# The OPL2 chip: ymfm (BSD-3-Clause) behind a small C interface (src/opl.h).
YMFM_DIR := third_party/ymfm
OPL_OBJ  := $(BUILD)/opl/ymfm_opl.o $(BUILD)/opl/ymfm_adpcm.o $(BUILD)/opl/ymfm_pcm.o $(BUILD)/opl/opl.o

CORE_SRC := src/lcz.c src/assets.c src/screen.c src/gfx.c src/title.c src/tkrand.c src/intro0.c src/intro1.c src/intro2.c src/intro3.c src/intro4.c src/intro5.c src/intro6.c src/subs.c src/slide.c src/voc.c src/voice.c src/audio.c src/cmf.c src/music.c src/menu.c src/credits.c
GAME_SRC := $(CORE_SRC) src/platform_sdl.c src/main.c

all: $(BUILD)/3k

$(BUILD)/opl/opl.o: src/opl.cpp src/opl.h $(YMFM_DIR)/*.h $(YMFM_DIR)/*.ipp | $(BUILD)/opl
	$(CXX) $(CXXFLAGS) -I$(YMFM_DIR) -Isrc -c $< -o $@

$(BUILD)/opl/%.o: $(YMFM_DIR)/%.cpp $(YMFM_DIR)/*.h $(YMFM_DIR)/*.ipp | $(BUILD)/opl
	$(CXX) $(CXXFLAGS) -I$(YMFM_DIR) -c $< -o $@

$(BUILD)/3k: $(GAME_SRC) src/*.h $(OPL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -Isrc $(GAME_SRC) $(OPL_OBJ) -o $@ $(SDL_LIBS) $(CXXLIB)

$(BUILD)/test_core: $(CORE_SRC) tests/test_core.c src/*.h $(OPL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(CORE_SRC) tests/test_core.c $(OPL_OBJ) -o $@ $(CXXLIB)

# Decode a VOC file to WAV (tests/voc_dump.c), to listen to it or compare it with another decoder.
$(BUILD)/voc_dump: src/voc.c tests/voc_dump.c src/voc.h | $(BUILD)
	$(CC) $(CFLAGS) -Isrc src/voc.c tests/voc_dump.c -o $@

$(BUILD)/test_audio: $(CORE_SRC) tests/test_audio.c src/*.h $(OPL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(CORE_SRC) tests/test_audio.c $(OPL_OBJ) -o $@ $(CXXLIB)

$(BUILD)/test_cmf: $(CORE_SRC) tests/test_cmf.c src/*.h $(OPL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(CORE_SRC) tests/test_cmf.c $(OPL_OBJ) -o $@ $(CXXLIB)

$(BUILD)/test_music: $(CORE_SRC) tests/test_music.c src/*.h $(OPL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(CORE_SRC) tests/test_music.c $(OPL_OBJ) -o $@ $(CXXLIB)

# Render the intro song to a WAV file, to listen to it without the game (tests/music_dump.c).
$(BUILD)/music_dump: $(CORE_SRC) tests/music_dump.c src/*.h $(OPL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(CORE_SRC) tests/music_dump.c $(OPL_OBJ) -o $@ $(CXXLIB)

$(BUILD)/test_menu: $(CORE_SRC) tests/test_menu.c src/*.h $(OPL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(CORE_SRC) tests/test_menu.c $(OPL_OBJ) -o $@ $(CXXLIB)

$(BUILD)/test_credits: $(CORE_SRC) tests/test_credits.c src/*.h $(OPL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(CORE_SRC) tests/test_credits.c $(OPL_OBJ) -o $@ $(CXXLIB)

$(BUILD)/test_gfx: $(CORE_SRC) tests/test_gfx.c src/*.h $(OPL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(CORE_SRC) tests/test_gfx.c $(OPL_OBJ) -o $@ $(CXXLIB)

# The same program with no window and no SDL (see tests/platform_null.c). Only the stepped
# modes (--step-dump) work. Build it with sanitizers:
#   make clean && make build/3k_headless CFLAGS="-std=c99 -O1 -g -fsanitize=address,undefined" \
#        CXXFLAGS="-std=c++14 -O1 -g -fsanitize=address,undefined"
$(BUILD)/3k_headless: $(CORE_SRC) tests/platform_null.c src/main.c src/*.h $(OPL_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc $(CORE_SRC) tests/platform_null.c src/main.c $(OPL_OBJ) -o $@ $(CXXLIB)

# Every test binary runs under a 5-minute limit (perl's alarm; macOS has no `timeout`): a test that
# spins fails with a signal instead of hanging the build, as test_music once did.
LIMIT := perl -e 'alarm 300; exec @ARGV' --

test: $(BUILD)/test_core $(BUILD)/test_gfx $(BUILD)/test_audio $(BUILD)/test_cmf $(BUILD)/test_music $(BUILD)/test_menu $(BUILD)/test_credits
	$(LIMIT) $(BUILD)/test_core original_bin
	$(LIMIT) $(BUILD)/test_gfx
	$(LIMIT) $(BUILD)/test_audio original_bin
	$(LIMIT) $(BUILD)/test_cmf original_bin
	$(LIMIT) $(BUILD)/test_music original_bin
	$(LIMIT) $(BUILD)/test_menu original_bin
	$(LIMIT) $(BUILD)/test_credits original_bin

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/opl:
	mkdir -p $(BUILD)/opl

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
