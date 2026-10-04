#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cargo fmt --all -- --check
cargo clippy --workspace --all-targets -- -D warnings
cargo test --workspace --locked
cargo build --release --workspace --locked
cc -std=c11 -Wall -Wextra -Werror -Iinclude tests/ffi_smoke.c \
    -Ltarget/release -lmyswy_ime -Wl,-rpath,"$PWD/target/release" -o target/ffi-smoke
target/ffi-smoke
target/release/myswy --query nihao
target/release/myswy --query "xi'an"
