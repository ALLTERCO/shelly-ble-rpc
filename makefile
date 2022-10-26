DEPS	= deps
BUILD	= build

.PHONY: build

build:
			mos build --local

clean: 
			rm -rf $(DEPS)
			rm -rf $(BUILD)

localflash:
			mos flash

clang-format:
			clang-format -i -style=file src/*.c src/*.h