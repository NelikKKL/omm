# omm - build helpers. Needs: gcc/make for tests, emcc (Emscripten) for the JS bundle.

SRC     := $(wildcard src/*.c)
HDR     := $(wildcard src/*.h)
CFLAGS  ?= -std=c99 -O1 -g -Wall -Wextra -Wpedantic -Wshadow

# Pure JavaScript output (wasm2js), one file, no .wasm.
EMFLAGS := -O3 -sWASM=0 -sSINGLE_FILE=1 -sMODULARIZE=1 -sEXPORT_NAME=OmmCore \
           -sENVIRONMENT=web,worker,node -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=524288 \
           -sFILESYSTEM=0 -sEXPORTED_RUNTIME_METHODS=cwrap,HEAPF32,HEAPU32 --no-entry

.PHONY: all test test-native smoke serve clean

all: dist/omm.js

build/test_core: $(SRC) $(HDR) test/test_core.c
	mkdir -p build
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	    -fno-omit-frame-pointer -Isrc $(SRC) test/test_core.c -lm -o $@

test-native: build/test_core
	./build/test_core

dist/omm-core.js: $(SRC) $(HDR)
	mkdir -p dist
	emcc $(SRC) $(EMFLAGS) -o $@

# core + wrapper in one classic script
dist/omm.js: dist/omm-core.js js/omm.js
	( cat dist/omm-core.js; echo; cat js/omm.js ) > $@

smoke: dist/omm.js
	node test/smoke.js

test: test-native smoke

serve:
	python3 -m http.server 8000

clean:
	rm -rf build dist
