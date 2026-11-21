.PHONY: test-tools m0-spike clean

test-tools:
	python3 -m unittest discover -s tests/tools -p "test_*.py" -v

m0-spike:
	mkdir -p build
	zig cc -target mipsel-linux-musleabihf -mcpu=mips32r2 -mfp64 -mno-odd-spreg -mnan=2008 -static -ffp-contract=off -O2 -s -std=c11 -Wall -Wextra -Werror spike/m0_bench.c -o build/m0_mq_ui -lm
	python3 -m tools.dial_os_mac.elf build/m0_mq_ui

clean:
	rm -rf build/
