#!/usr/bin/env python3
"""Build one offline Windows x64 installer EXE; never register it on the host."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import tempfile
import tomllib
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REVISION = 7
BINARIES = {"myswy_tsf.dll": True, "myswy_probe.exe": False, "myswy_settings.exe": False}


def validate_pe(path: Path, dll: bool) -> None:
    data = path.read_bytes()
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError(f"Not a PE image: {path}")
    offset = struct.unpack_from("<I", data, 60)[0]
    if offset > len(data) - 26 or data[offset : offset + 4] != b"PE\0\0":
        raise ValueError(f"Invalid PE header: {path}")
    machine = struct.unpack_from("<H", data, offset + 4)[0]
    characteristics = struct.unpack_from("<H", data, offset + 22)[0]
    magic = struct.unpack_from("<H", data, offset + 24)[0]
    if machine != 0x8664 or magic != 0x20B or bool(characteristics & 0x2000) != dll:
        raise ValueError(f"Expected x64 {'DLL' if dll else 'EXE'}: {path}")


def make_runtime_licenses(stage: Path, sysroot: Path, notices: list[Path], makensis: str) -> None:
    docs = sysroot / "share/doc/rust"
    rust_copyright = docs / "COPYRIGHT-library.html"
    licenses = sorted((docs / "licenses").glob("*.txt"))
    if not rust_copyright.is_file() or not licenses:
        raise ValueError("Rust runtime notices are missing; install the rust-docs component")
    with zipfile.ZipFile(stage / "RUNTIME_LICENSES.zip", "x", zipfile.ZIP_DEFLATED) as archive:
        archive.write(rust_copyright, "rust/COPYRIGHT-library.html")
        for license_file in licenses:
            archive.write(license_file, f"rust/licenses/{license_file.name}")
        for source in ("rime-pinyin-simp", "jieba"):
            vocabulary = ROOT / "data/sources" / source
            for name in ("LICENSE", "README.md", "SOURCE.json"):
                archive.write(vocabulary / name, f"vocabulary/{source}/{name}")
        archive.write(ROOT / "data/README.md", "vocabulary/README.md")
        for index, notice in enumerate(notices):
            if not notice.is_file():
                raise ValueError(f"Missing runtime notice: {notice}")
            archive.write(notice, f"toolchain/{index + 1}-{notice.parent.name}-{notice.name}")
        components = Path(makensis).resolve().parent / "CHENGYIN_AMD64_COMPONENTS.json"
        if components.is_file():
            archive.write(components, "installer/AMD64_COMPONENTS.json")


def verify_archive(path: Path) -> None:
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(set(names)) != len(names) or any("/" in n or "\\" in n or n in (".", "..") for n in names):
            raise ValueError("Preview ZIP must contain unique flat paths")
        hashes = json.loads(archive.read("SHA256SUMS.json"))
        if set(hashes) != set(names) - {"SHA256SUMS.json"}:
            raise ValueError("Checksum manifest does not cover the complete package")
        for name, expected in hashes.items():
            if hashlib.sha256(archive.read(name)).hexdigest().upper() != expected:
                raise ValueError(f"Checksum mismatch: {name}")
        if archive.testzip() is not None:
            raise ValueError("ZIP CRC check failed")


def nsis_quote(value: str) -> str:
    # Compile-time NSIS strings: escape literal dollar signs and quotes.
    return value.replace("$", "$$").replace('"', '$\\"')


def build_installer(stage: Path, output: Path, makensis: str, version: str, revision: int) -> None:
    with tempfile.TemporaryDirectory(prefix="myswy-nsis-") as temporary:
        include = Path(temporary) / "payload.nsh"
        names = sorted(p.name for p in stage.iterdir())
        lines = ["!macro ExtractPayload"]
        for name in names:
            lines.append(f'  File "{nsis_quote(str(stage / name))}"')
        lines.extend(["!macroend", "!macro DeletePayload DIR"])
        for name in names:
            lines.append('  Delete /REBOOTOK "${DIR}\\' + name + '"')
        lines.append("!macroend")
        include.write_text("\n".join(lines) + "\n", encoding="utf-8")
        prefix = "/" if platform.system() == "Windows" else "-"
        numeric_version = version.split("-", 1)[0] + f".{revision}"
        options = {
            "PAYLOAD_INCLUDE": str(include), "PAYLOAD_DIR": str(stage), "OUTPUT": str(output),
            "VERSION": version, "NUMERIC_VERSION": numeric_version, "REVISION": str(revision),
            "SIZE_KIB": str(sum(p.stat().st_size for p in stage.iterdir()) // 1024 + 256),
        }
        # Paths are structured argv entries; no shell interpolation.
        # The script and generated include are UTF-8. Windows otherwise reads
        # BOM-less source in the system code page, corrupting Chinese labels.
        subprocess.run([makensis, prefix + "INPUTCHARSET", "UTF8", prefix + "WX", prefix + "V2",
                        *[prefix + "D" + key + "=" + value for key, value in options.items()],
                        str(ROOT / "platforms/windows/installer.nsi")], check=True)
    validate_pe(output, False)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/windows-msvc")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--makensis", default="makensis")
    parser.add_argument("--toolchain", choices=("msvc", "gnu"))
    parser.add_argument("--runtime-notice", type=Path, action="append", default=[])
    parser.add_argument("--nsis-notice", type=Path, required=False)
    parser.add_argument("--rust-sysroot", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/packages")
    parser.add_argument("--revision", type=int, default=REVISION)
    parser.add_argument("--verify", type=Path, help="Validate a legacy preview1 ZIP only")
    args = parser.parse_args()
    if args.verify:
        verify_archive(args.verify)
        print(f"Verified {args.verify}")
        return
    if args.toolchain is None or args.nsis_notice is None or not 1 <= args.revision <= 999:
        parser.error("Installer needs --toolchain, --nsis-notice and revision 1..999")
    if args.toolchain == "gnu" and len(args.runtime_notice) < 2:
        parser.error("GNU packages require GCC and MinGW copyright notices (--runtime-notice)")
    version = tomllib.loads((ROOT / "Cargo.toml").read_text())["workspace"]["package"]["version"]
    tag = f"{version}-preview{args.revision}"
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / f"chengyin-windows-x64-{tag}-{args.toolchain}.exe"
    if output.exists():
        raise FileExistsError(f"Refusing to overwrite {output}")
    sysroot = args.rust_sysroot or Path(subprocess.check_output(["rustc", "--print", "sysroot"], text=True).strip())
    with tempfile.TemporaryDirectory(prefix="myswy-windows-") as temporary:
        stage = Path(temporary) / "stage"
        subprocess.run([args.cmake, "--install", str(args.build_dir.resolve()), "--config", "Release", "--prefix", str(stage)], check=True)
        for name, dll in BINARIES.items():
            validate_pe(stage / name, dll)
        make_runtime_licenses(stage, sysroot, args.runtime_notice, args.makensis)
        (stage / "INSTALLER_LICENSE.txt").write_bytes(args.nsis_notice.read_bytes())
        info = {
            "product": "澄音输入法 / Chengyin IME", "version": tag, "architecture": "x64", "toolchain": args.toolchain,
            "build_host": platform.system(),
            "rust": subprocess.check_output(["rustc", "--version"], text=True).strip(),
            "installer": subprocess.check_output([args.makensis, "-VERSION" if platform.system() != "Windows" else "/VERSION"], text=True).strip(),
            "validation": "See README.md and docs/STATUS.md; internal tests do not establish real desktop compatibility.",
            "signed": False,
        }
        (stage / "BUILD_INFO.json").write_text(json.dumps(info, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest().upper() for p in sorted(stage.iterdir())}
        (stage / "SHA256SUMS.txt").write_text("".join(f"{digest}  {name}\n" for name, digest in hashes.items()), encoding="ascii")
        candidate = Path(temporary) / "myswy-package.exe"
        build_installer(stage, candidate, args.makensis, tag, args.revision)
        with output.open("xb") as destination:
            try:
                destination.write(candidate.read_bytes())
            except BaseException:
                destination.close()
                output.unlink(missing_ok=True)
                raise
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    print(f"{output}\nSHA256 {digest}\n{output.stat().st_size} bytes")


if __name__ == "__main__":
    main()
