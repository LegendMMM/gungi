#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
RAYLIB_VERSION="5.5"
RAYLIB_DIR="$ROOT/.deps/src/raylib-$RAYLIB_VERSION"
RAYLIB_ZIP="$ROOT/.deps/downloads/raylib-$RAYLIB_VERSION.zip"
RAYLIB_LIB="$RAYLIB_DIR/src/libraylib.a"

cd "$ROOT"

if ! command -v clang >/dev/null 2>&1; then
  echo "[error] clang was not found. Install Xcode Command Line Tools first:"
  echo "        xcode-select --install"
  exit 1
fi

if ! command -v make >/dev/null 2>&1; then
  echo "[error] make was not found. Install Xcode Command Line Tools first:"
  echo "        xcode-select --install"
  exit 1
fi

if [ ! -f "$RAYLIB_DIR/src/raylib.h" ]; then
  echo "[setup] Downloading raylib..."
  mkdir -p "$ROOT/.deps/downloads" "$ROOT/.deps/src"
  curl -L "https://github.com/raysan5/raylib/archive/refs/tags/$RAYLIB_VERSION.zip" -o "$RAYLIB_ZIP"
  rm -rf "$RAYLIB_DIR"
  unzip -q "$RAYLIB_ZIP" -d "$ROOT/.deps/src"
fi

if [ ! -f "$RAYLIB_LIB" ]; then
  echo "[setup] Building raylib..."
  make -C "$RAYLIB_DIR/src" PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC
fi

echo "[setup] Building Gungi..."
mkdir -p tests tools

clang -O3 -std=c99 -Wall -Wextra -pedantic \
  -Isrc \
  tests/test_rules.c src/ai_core.c src/gungi_rules.c \
  -o tests/test_rules

clang -O3 -std=c99 -Wall -Wextra -pedantic \
  -Isrc -I"$RAYLIB_DIR/src" \
  src/ai_core.c src/q_model.c src/main.c src/gungi_rules.c "$RAYLIB_LIB" \
  -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo \
  -o gungi

clang -O3 -std=c99 -Wall -Wextra -pedantic \
  -Isrc \
  tools/train_q.c src/q_model.c src/ai_core.c src/gungi_rules.c \
  -o tools/train_q

clang -O3 -std=c99 -Wall -Wextra -pedantic \
  -Isrc \
  tools/eval_q.c src/q_model.c src/ai_core.c src/gungi_rules.c \
  -o tools/eval_q

clang -O3 -std=c99 -Wall -Wextra -pedantic \
  -Isrc \
  tools/compare_model_minimax.c src/q_search.c src/q_model.c src/ai_core.c src/gungi_rules.c \
  -o tools/compare_model_minimax

clang -O3 -std=c99 -Wall -Wextra -pedantic \
  -Isrc \
  tools/train_q_from_hybrid.c src/q_search.c src/q_model.c src/ai_core.c src/gungi_rules.c \
  -o tools/train_q_from_hybrid

echo "[setup] Running rules tests..."
./tests/test_rules

if [ "${1:-}" = "--no-run" ]; then
  echo "[setup] Done. Built $ROOT/gungi."
  exit 0
fi

echo "[setup] Starting Gungi..."
./gungi
