#!/usr/bin/env python3
"""Build a native Debian preview from an already configured CMake build.

Requires Python 3.11+, CMake, dpkg-dev and matching Fcitx development packages.
No root access or system installation. Dependency resolution fails closed.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import tomllib

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "fcitx5-chengyin"


def run(command, **kwargs):
    return subprocess.run(command, check=True, text=True, **kwargs)


def build(args):
    build_dir = args.build_dir.resolve()
    cache = (build_dir / "CMakeCache.txt").read_text()
    if not re.search(r"^CMAKE_INSTALL_PREFIX:PATH=/usr$", cache, re.M):
        raise ValueError("Configure with -DCMAKE_INSTALL_PREFIX=/usr before packaging")
    architecture = run(["dpkg", "--print-architecture"], capture_output=True).stdout.strip()
    default_version = tomllib.loads((ROOT / "Cargo.toml").read_text())["workspace"]["package"]["version"] + "-1"
    version = args.version or default_version
    run(["dpkg", "--validate-version", version])
    if any(c in args.maintainer for c in "\r\n"):
        raise ValueError("Maintainer must be a single control-file line")
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / f"{PACKAGE}_{version}_{architecture}.deb"
    if output.exists():
        raise ValueError(f"Refusing to overwrite existing package: {output}")

    with tempfile.TemporaryDirectory(prefix="chengyin-package-") as temporary:
        work = Path(temporary)
        stage = work / "stage"
        environment = dict(os.environ, DESTDIR=str(stage), LC_ALL="C")
        run([args.cmake, "--install", str(build_dir)], env=environment)
        modules = list(stage.glob("usr/lib*/**/fcitx5/chengyin.so"))
        if len(modules) != 1:
            raise ValueError("Expected exactly one Fcitx module under /usr/lib or /usr/lib64")
        # Native packages only: reject a module from an incompatible build target.
        header = run(["readelf", "-h", str(modules[0])], capture_output=True, env=environment).stdout
        machine = {"amd64": "Advanced Micro Devices X86-64", "arm64": "AArch64"}.get(architecture)
        if machine is None or not re.search(r"Machine:\s+" + re.escape(machine) + r"\s*$", header, re.M):
            raise ValueError("Only matching native Debian amd64/arm64 builds are currently packaged")

        debian = work / "debian"
        debian.mkdir()
        (debian / "control").write_text(
            f"Source: {PACKAGE}\nSection: utils\nPriority: optional\nMaintainer: {args.maintainer}\n\n"
            f"Package: {PACKAGE}\nArchitecture: any\nDescription: Chengyin pinyin development preview\n"
        )
        command = ["dpkg-shlibdeps", "-O", "--warnings=0", "-e" + str(modules[0])]
        command.extend("-l" + str(path.resolve()) for path in args.library_dir)
        if args.shlibs_local:
            command.append("-L" + str(args.shlibs_local.resolve()))
        variables = run(command, cwd=work, capture_output=True, env=environment).stdout
        match = re.search(r"^shlibs:Depends=(.+)$", variables, re.M)
        if not match:
            raise ValueError("dpkg-shlibdeps did not resolve runtime dependencies")

        files = sorted(path for path in stage.rglob("*") if path.is_file())
        if any(path.is_symlink() or "usr" not in path.relative_to(stage).parts[:1] for path in files):
            raise ValueError("Unexpected staged file or symlink")
        control_dir = stage / "DEBIAN"
        control_dir.mkdir()
        installed_size = (sum(path.stat().st_size for path in files) + 1023) // 1024
        (control_dir / "control").write_text(
            f"Package: {PACKAGE}\nVersion: {version}\nArchitecture: {architecture}\n"
            f"Maintainer: {args.maintainer}\nSection: utils\nPriority: optional\n"
            f"Installed-Size: {installed_size}\nDepends: fcitx5 (>= 5.1), {match[1]}\n"
            "Recommends: fcitx5-config-qt\nHomepage: https://github.com/zzttzzmyswy/myswyIm\n"
            "Description: Chengyin full-pinyin Fcitx 5 development preview\n"
            " Shared Rust input core with background custom TSV dictionary loading.\n"
            " Includes a small demo vocabulary; real desktop validation is pending.\n"
        )
        (control_dir / "md5sums").write_text("".join(
            f"{hashlib.md5(path.read_bytes()).hexdigest()}  {path.relative_to(stage)}\n" for path in files
        ))
        # Developer environments may use umask 077; package contents must remain
        # readable by the non-root Fcitx process after installation.
        for directory in [stage, *[p for p in stage.rglob("*") if p.is_dir()]]:
            directory.chmod(0o755)
        for path in files + list(control_dir.iterdir()):
            path.chmod(0o755 if path == modules[0] else 0o644)
        run(["dpkg-deb", "--root-owner-group", "-Zxz", "--build", str(stage), str(output)])
    print(output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/fcitx5")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/packages")
    parser.add_argument("--cmake", default=shutil.which("cmake") or "cmake")
    parser.add_argument("--version", help="Debian version; defaults to workspace version + -1")
    parser.add_argument("--maintainer", default="Chengyin IM development build <maintainer@example.invalid>")
    parser.add_argument("--library-dir", type=Path, action="append", default=[], help="Extra SDK library search directory")
    parser.add_argument("--shlibs-local", type=Path, help="Explicit shlibs metadata for an unpacked SDK")
    args = parser.parse_args()
    try:
        build(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Package build failed: {error}\n{getattr(error, 'stderr', '') or ''}")


if __name__ == "__main__":
    main()
