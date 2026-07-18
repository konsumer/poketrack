# I use this as a wrapper around common taks.

.PHONY: help build build-web clean serve format format-check test run plugins
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
	cd clap_examples/robotalk && cargo build --target wasm32-unknown-unknown --release --lib
	mkdir -p webroot/tts
	wasm-bindgen clap_examples/robotalk/target/wasm32-unknown-unknown/release/robotalk.wasm --out-dir webroot/tts --out-name robotalk --target web --no-typescript

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

plugins: ## Build bundled example WCLAP plugins into examples/plugins/
	cd clap_examples/karplus && [ -d node_modules ] || npm install
	cd clap_examples/karplus && npm run asbuild:release
	rustup target add wasm32-wasip1
	cd clap_examples/robotalk && cargo build --target wasm32-wasip1 --release --lib
	mkdir -p examples/plugins
	cp clap_examples/karplus/build/release.wasm examples/plugins/karp.wclap.wasm
	cp clap_examples/robotalk/target/wasm32-wasip1/release/robotalk.wasm examples/plugins/robotalk.wclap.wasm
