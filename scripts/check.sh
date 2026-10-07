#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# A stale or hand-edited release version must fail here rather than ship (R13).
python3 scripts/version.py --check
cargo fmt --all -- --check
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace --locked
cargo build --release --workspace --locked
cc -std=c11 -Wall -Wextra -Werror -Iinclude tests/ffi_smoke.c \
    -Ltarget/release -lchengyin_ime -Wl,-rpath,"$PWD/target/release" -o target/ffi-smoke
target/ffi-smoke
target/release/chengyin --query nihao
target/release/chengyin --query "xi'an"
