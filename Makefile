# I use this as a wrapper around common taks.

# Force every recipe line through a real shell. Without this, GNU Make's
# no-shell-needed fast path execve()s the first PATH match itself and,
# unlike bash, doesn't skip past a non-executable/directory match -- and
# emsdk's PATH entries include upstream/emscripten/cmake, a *directory*
# (CMake toolchain modules) that shadows the real `cmake` binary and
# breaks `cmake --build` in CI with a bogus "Permission denied".
SHELL := /bin/bash

.PHONY: help build build-web clean serve format format-check test run plugins theme-shots
.DEFAULT_GOAL := help

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}'

build: ## Build native release
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel

build-web: ## Build web (Emscripten) release, plus the ROBOTALK text-to-pattern tool in webroot/tts/
	emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release -DPLATFORM=Web
	cmake --build build-web --parallel
	cp build-web/poketrack.mjs webroot/poketrack.mjs
	rustup target add wasm32-unknown-unknown
	command -v wasm-bindgen >/dev/null || cargo install wasm-bindgen-cli --version 0.2.100 --locked
	cd plugins/robotalk && cargo build --target wasm32-unknown-unknown --release --lib
	mkdir -p webroot/tts
	wasm-bindgen plugins/robotalk/target/wasm32-unknown-unknown/release/robotalk.wasm --out-dir webroot/tts --out-name robotalk --target web --no-typescript

clean: ## Delete built files
	rm -rf build-web build

run: build ## Build & run native build
	./build/poketrack || ./build/poketrack.app/Contents/MacOS/poketrack

serve: build-web ## Build & run watching web build
	npx -y live-server webroot

test: ## Build & run quick sanity tests
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel --target poketrack_tests
	cd build && ./poketrack_tests

format: ## Format all C/H source files with clang-format
	find src -name "*.c" -o -name "*.h" | xargs clang-format -i

format-check: ## Fail if any source file is not clang-format clean (for CI)
	find src -name "*.c" -o -name "*.h" | xargs clang-format --dry-run --Werror

theme-shots: ## Render a PNG preview of every examples/themes/*.ptt into art/themes/
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel --target theme_shot
	mkdir -p art/themes
	for f in examples/themes/*.ptt; do ./build/theme_shot "$$f" art/themes; done
	./build/theme_shot "examples/theme.ptt" art/themes

plugins: ## Build bundled example WCLAP plugins into examples/plugins/
	cd plugins/karplus && [ -d node_modules ] || npm install
	cd plugins/karplus && npm run asbuild:release
	cd plugins/subsynth && [ -d node_modules ] || npm install
	cd plugins/subsynth && npm run asbuild:release
	rustup target add wasm32-wasip1
	cd plugins/robotalk && cargo build --target wasm32-wasip1 --release --lib
	mkdir -p examples/plugins
	cp plugins/karplus/build/release.wasm examples/plugins/karp.wclap.wasm
	cp plugins/subsynth/build/release.wasm examples/plugins/subsynth.wclap.wasm
	cp plugins/robotalk/target/wasm32-wasip1/release/robotalk.wasm examples/plugins/robotalk.wclap.wasm
	command -v pd2ast >/dev/null && command -v pdast2wclap >/dev/null && [ -x "$${WASI_SDK_PATH:-/opt/wasi-sdk}/bin/clang" ] && ( \
		plugins/pd2wclap/build.sh plugins/pd2wclap/patches/supersaw.pd pd-supersaw && \
		plugins/pd2wclap/build.sh plugins/pd2wclap/patches/osc.pd pd-osc && \
		plugins/pd2wclap/build.sh plugins/pd2wclap/patches/vcf.pd pd-vcf && \
		cp plugins/pd2wclap/build/pd-supersaw.wasm examples/plugins/pd-supersaw.wclap.wasm && \
		cp plugins/pd2wclap/build/pd-osc.wasm examples/plugins/pd-osc.wclap.wasm && \
		cp plugins/pd2wclap/build/pd-vcf.wasm examples/plugins/pd-vcf.wclap.wasm \
	) || echo "skipping pd2wclap demos (see plugins/pd2wclap/README.md#install) — pd2ast/pdast2wclap/wasi-sdk not found"
