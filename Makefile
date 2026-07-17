# I use this as a wrapper around common taks.

.PHONY: help build build-web clean serve format format-check test run plugins
.DEFAULT_GOAL := help

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}'

build: ## Build native release
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel

build-web: ## Build web (Emscripten) release
	emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release -DPLATFORM=Web
	cmake --build build-web --parallel

clean: ## Delete built files
	rm -rf build-web build

run: build ## Build & run native build
	./build/poketrack || ./build/poketrack.app/Contents/MacOS/poketrack

serve: build-web ## Build & run watching web build
	npx -y live-server webroot --mount=/poketrack.mjs:./build-web/poketrack.mjs

test: ## Build & run quick sanity tests
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel --target poketrack_tests
	cd build && ./poketrack_tests

format: ## Format all C/H source files with clang-format
	find src -name "*.c" -o -name "*.h" | xargs clang-format -i

format-check: ## Fail if any source file is not clang-format clean (for CI)
	find src -name "*.c" -o -name "*.h" | xargs clang-format --dry-run --Werror

plugins: ## Build bundled example WCLAP plugins into examples/plugins/
	cd test_clap_plugin/karplus && [ -d node_modules ] || npm install
	cd test_clap_plugin/karplus && npm run asbuild:release
	mkdir -p examples/plugins
	cp test_clap_plugin/karplus/build/release.wasm examples/plugins/karp.wclap.wasm
