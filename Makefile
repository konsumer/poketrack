# I use this as a wrapper around common taks.

.PHONY: help build build-web clean serve format
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
	./build/raypoketrack || ./build/raypoketrack.app/Contents/MacOS/raypoketrack

serve: build-web ## Build & run watching web build
	npx -y live-server webroot --mount=/raypoketrack.mjs:./build-web/raypoketrack.mjs

format: ## Format all C/H source files with clang-format
	find . -name "*.c" -o -name "*.h" | grep -v build | xargs clang-format -i
