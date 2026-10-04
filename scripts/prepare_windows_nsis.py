#!/usr/bin/env python3
"""Prepare NSIS 3.11's missing AMD64 stubs in an isolated build directory."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

# Verified against Debian trixie's signed Packages index. These are Windows PE
# stubs/plugins, not Linux executables. Keep the compiler and components at 3.11.
URL = "https://deb.debian.org/debian/pool/main/n/nsis/nsis-common_3.11-1_all.deb"
SHA256 = "103a3284c1a5356efa0aba90fdfc391c3cde8e7df8726377cef0f98471584047"
SIZE = 1168188
MARKER = "CHENGYIN_AMD64_COMPONENTS.json"
REQUIRED = {"Stubs/zlib-amd64-unicode", "Stubs/lzma_solid-amd64-unicode",
            "Plugins/amd64-unicode/System.dll", "Plugins/amd64-unicode/nsDialogs.dll"}


def verified_package(data: bytes) -> bytes:
    if len(data) != SIZE or hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError("NSIS component archive does not match the pinned Debian SHA-256")
    return data


def component_files(data: bytes) -> dict[str, bytes]:
    if not data.startswith(b"!<arch>\n"):
        raise ValueError("Invalid Debian archive")
    offset = 8
    payload = None
    while offset < len(data):
        header = data[offset:offset + 60]
        if len(header) != 60 or header[58:] != b"`\n":
            raise ValueError("Invalid archive member header")
        name = header[:16].decode("ascii").strip().rstrip("/")
        length = int(header[48:58])
        offset += 60
        if length < 0 or offset + length > len(data):
            raise ValueError("Truncated archive member")
        if name == "data.tar.xz":
            if payload is not None:
                raise ValueError("Duplicate data archive")
            payload = data[offset:offset + length]
        offset += length + length % 2
    if payload is None:
        raise ValueError("Missing data archive")
    files: dict[str, bytes] = {}
    with tarfile.open(fileobj=io.BytesIO(payload), mode="r:xz") as archive:
        for member in archive:
            # Never extract a tar path. Only copy allowlisted regular files into
            # fixed relative paths, including when running on Windows.
            parts = PurePosixPath(member.name).parts
            if parts[:3] != ("usr", "share", "nsis"):
                continue
            relative = parts[3:]
            allowed = (len(relative) == 2 and relative[0] == "Stubs"
                       and relative[1].endswith("-amd64-unicode")) or (
                           len(relative) == 3 and relative[:2] == ("Plugins", "amd64-unicode")
                           and relative[2].endswith(".dll"))
            if not allowed or any("\\" in p or ":" in p or p in (".", "..") for p in relative):
                continue
            if not member.isfile() or member.size > 2 * 1024 * 1024:
                raise ValueError("Unexpected NSIS component type or size")
            name = "/".join(relative)
            if name in files:
                raise ValueError("Duplicate NSIS component")
            source = archive.extractfile(member)
            if source is None:
                raise ValueError("Unreadable NSIS component")
            files[name] = source.read()
    if not REQUIRED <= files.keys():
        raise ValueError("Missing AMD64 stubs or plugins")
    return files


def prepare(compiler: Path, output: Path, package: Path | None = None) -> Path:
    compiler = compiler.resolve(strict=True)
    output = output.resolve()
    compiler_hash = hashlib.sha256(compiler.read_bytes()).hexdigest()
    if output.exists():
        marker = json.loads((output / MARKER).read_text(encoding="utf-8"))
        if marker["package_sha256"] != SHA256 or marker["compiler_sha256"] != compiler_hash:
            raise ValueError("Existing NSIS build directory belongs to another toolchain")
        for name, expected in marker["files"].items():
            if hashlib.sha256((output / name).read_bytes()).hexdigest() != expected:
                raise ValueError(f"Prepared NSIS component was modified: {name}")
        return output / "makensis.exe"
    if package:
        data = package.read_bytes()
    else:
        with urllib.request.urlopen(URL, timeout=30) as response:
            data = response.read(SIZE + 1)
    files = component_files(verified_package(data))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nsis-components-", dir=output.parent) as temporary:
        stage = Path(temporary) / "nsis"
        shutil.copytree(compiler.parent, stage)
        for name, content in files.items():
            destination = stage / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(content)
        hashes = {name: hashlib.sha256(content).hexdigest() for name, content in files.items()}
        hashes["makensis.exe"] = compiler_hash
        marker = {"package": "nsis-common 3.11-1", "url": URL, "package_sha256": SHA256,
                  "compiler_sha256": compiler_hash, "files": hashes}
        (stage / MARKER).write_text(json.dumps(marker, indent=2) + "\n", encoding="utf-8")
        stage.rename(output)
    return output / "makensis.exe"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--makensis", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--package", type=Path, help="Offline copy of the pinned Debian archive")
    args = parser.parse_args()
    version = subprocess.check_output([str(args.makensis), "/VERSION"], text=True).strip()
    if version != "v3.11":
        raise ValueError("Pinned AMD64 components require the NSIS 3.11 compiler")
    compiler = prepare(args.makensis, args.output_dir, args.package)
    print(f"Prepared NSIS AMD64 components; SHA256 {SHA256}\n{compiler}")


if __name__ == "__main__":
    main()
