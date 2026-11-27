.PHONY: mq-ui test-tools test-c test clean

BUILD_DIRECTORY := build
PORTABLE_SOURCES := src/protocol.c src/theme.c src/font_atlas.c src/boot_sequence.c src/home_screen.c src/logger.c src/library_catalog.c src/library_scanner.c src/library_state.c src/jpeg_fast_thumb.c src/library_cover_cache.c src/library_controller.c src/render_primitives.c src/render_library.c src/render_home.c src/render.c src/app.c
TEST_SOURCES := tests/c/test_runner.c tests/c/test_protocol.c tests/c/test_boot_sequence.c tests/c/test_home_screen.c tests/c/test_logger.c tests/c/test_library_catalog.c tests/c/test_library_cover_cache.c tests/c/test_library_controller.c tests/c/test_render.c tests/c/test_app.c
LINUX_SOURCES := $(PORTABLE_SOURCES) src/platform_linux.c src/main.c

mq-ui:
	mkdir -p $(BUILD_DIRECTORY)
	zig cc -target mipsel-linux-musleabihf -mcpu=mips32r2 -mfp64 -mno-odd-spreg -mnan=2008 \
		-static -pthread -ffp-contract=off -std=c11 -D_GNU_SOURCE -Isrc -Wall -Wextra -Werror -O2 -s \
		$(LINUX_SOURCES) -lm -o $(BUILD_DIRECTORY)/mq_ui
	python3 -m tools.dial_os_mac.elf $(BUILD_DIRECTORY)/mq_ui

test-tools:
	python3 -m unittest discover -s tests/tools -p "test_*.py" -v

test-c:
	mkdir -p $(BUILD_DIRECTORY)
	zig cc -std=c11 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Isrc -Itests/c \
		-Wall -Wextra -Werror -pthread -fsanitize=address,undefined \
		$(PORTABLE_SOURCES) $(TEST_SOURCES) -lm -o $(BUILD_DIRECTORY)/test_phase1
	./$(BUILD_DIRECTORY)/test_phase1

test: test-tools test-c

clean:
	rm -rf $(BUILD_DIRECTORY)/
