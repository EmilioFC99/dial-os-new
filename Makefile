.PHONY: mq-ui test-tools test-c test clean

BUILD_DIRECTORY := build
PORTABLE_SOURCES := src/protocol.c src/theme.c src/font_atlas.c src/boot_sequence.c src/home_screen.c src/logger.c src/library_db.c src/library_scanner.c src/jpeg_fast_thumb.c src/library_cover_cache.c src/library_controller.c src/playback_engine.c src/now_playing_media.c src/render_primitives.c src/render_library.c src/render_home.c src/render.c src/app.c
TEST_SOURCES := tests/c/test_runner.c tests/c/test_protocol.c tests/c/test_boot_sequence.c tests/c/test_home_screen.c tests/c/test_logger.c tests/c/test_library_db.c tests/c/test_library_scanner_db.c tests/c/test_library_cover_cache.c tests/c/test_library_controller.c tests/c/test_playback_engine.c tests/c/test_now_playing_media.c tests/c/test_render.c tests/c/test_app.c
LINUX_SOURCES := $(PORTABLE_SOURCES) src/platform_linux.c src/main.c

# Third-party SQLite is compiled apart from DialOS code so -Werror never applies to it.
SQLITE_SOURCE := src/third_party/sqlite3.c
SQLITE_FLAGS := -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_DEPRECATED -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_DQS=0 -DSQLITE_ENABLE_MEMORY_MANAGEMENT
SQLITE_MIPS_OBJECT := $(BUILD_DIRECTORY)/sqlite3_mips.o
SQLITE_HOST_OBJECT := $(BUILD_DIRECTORY)/sqlite3_host.o

MIPS_TARGET_FLAGS := -target mipsel-linux-musleabihf -mcpu=mips32r2 -mfp64 -mno-odd-spreg -mnan=2008

$(SQLITE_MIPS_OBJECT): $(SQLITE_SOURCE)
	mkdir -p $(BUILD_DIRECTORY)
	zig cc $(MIPS_TARGET_FLAGS) -static -pthread -ffp-contract=off -D_GNU_SOURCE -O2 $(SQLITE_FLAGS) \
		-c $(SQLITE_SOURCE) -o $@

$(SQLITE_HOST_OBJECT): $(SQLITE_SOURCE)
	mkdir -p $(BUILD_DIRECTORY)
	zig cc -O2 -pthread $(SQLITE_FLAGS) -c $(SQLITE_SOURCE) -o $@

mq-ui: $(SQLITE_MIPS_OBJECT)
	mkdir -p $(BUILD_DIRECTORY)
	zig cc $(MIPS_TARGET_FLAGS) \
		-static -pthread -ffp-contract=off -std=c11 -D_GNU_SOURCE -Isrc -Isrc/third_party -Wall -Wextra -Werror -O2 -s \
		$(LINUX_SOURCES) $(SQLITE_MIPS_OBJECT) -lm -o $(BUILD_DIRECTORY)/mq_ui
	python3 -m tools.dial_os_mac.elf $(BUILD_DIRECTORY)/mq_ui

test-tools:
	python3 -m unittest discover -s tests/tools -p "test_*.py" -v

test-c: $(SQLITE_HOST_OBJECT)
	mkdir -p $(BUILD_DIRECTORY)
	zig cc -std=c11 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Isrc -Isrc/third_party -Itests/c \
		-Wall -Wextra -Werror -pthread -fsanitize=address,undefined \
		$(PORTABLE_SOURCES) $(TEST_SOURCES) $(SQLITE_HOST_OBJECT) -lm -o $(BUILD_DIRECTORY)/test_phase1
	./$(BUILD_DIRECTORY)/test_phase1

test: test-tools test-c

clean:
	rm -rf $(BUILD_DIRECTORY)/
