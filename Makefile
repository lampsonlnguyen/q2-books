Q2_CC ?= clang
Q2_LD ?= ld.lld
Q2_FLAGS = --target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008 -mno-abicalls -fno-pic -G0 -ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables
CFLAGS = -Oz -Wall -Wextra -Werror -Wno-unused-variable -std=c11 -Isrc
.PHONY: all clean
all: dist/crosspod.zip
build/%.o: src/%.c src/crosspod.h src/font.h
	mkdir -p build
	$(Q2_CC) $(Q2_FLAGS) $(CFLAGS) -c $< -o $@
dist/.crosspod/q2-crosspod: build/platform.o build/reader.o build/render.o build/epub.o
	mkdir -p dist/.crosspod
	$(Q2_LD) -m elf32ltsmip -static -e __start -s -z noexecstack -o $@ $^
dist/.crosspod/q2app: q2app
	mkdir -p dist/.crosspod
	cp q2app $@
dist/.crosspod/book.txt: sample/book.txt
	mkdir -p dist/.crosspod
	cp $< $@
dist/crosspod.zip: dist/.crosspod/q2-crosspod dist/.crosspod/q2app dist/.crosspod/book.txt
	cd dist && zip -q crosspod.zip .crosspod/q2-crosspod .crosspod/q2app .crosspod/book.txt
clean:
	rm -rf build dist
