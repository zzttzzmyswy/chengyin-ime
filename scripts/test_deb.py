#!/usr/bin/env python3
"""Exercise Debian package lifecycle in an empty temporary root, never on the host.

Dependencies are deliberately skipped in this file-only fixture; it does not
validate dependency installation, a running daemon or a real desktop session.
"""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


def run(command):
    result = subprocess.run(command, check=True, text=True, capture_output=True)
    return result.stdout


def test_package(package):
    with tempfile.TemporaryDirectory(prefix="chengyin-dpkg-test-") as temporary:
        work = Path(temporary)
        root = work / "root"
        (root / "var/lib/dpkg").mkdir(parents=True)
        (root / "var/lib/dpkg/status").touch()
        user_config = root / "home/test/.config/fcitx5/conf/chengyin.conf"
        user_config.parent.mkdir(parents=True)
        user_config.write_text("DictionaryPath=/home/test/private.tsv\n")
        # Container dpkg defaults often exclude documentation. Include the full
        # payload in THIS temporary root so the manifest check is meaningful.
        dpkg = ["dpkg", "--root=" + str(root), "--force-not-root", "--force-bad-path",
                "--force-depends", "--path-include=*"]

        # Reject maintainer scripts before executing package management in a fixture.
        extracted = work / "extracted"
        run(["dpkg-deb", "--raw-extract", str(package), str(extracted)])
        assert {p.name for p in (extracted / "DEBIAN").iterdir()} == {"control", "md5sums"}
        metadata = run(["dpkg-deb", "--field", str(package)])
        assert "Package: fcitx5-chengyin\n" in metadata
        assert "Depends: fcitx5 (>= 5.1)," in metadata
        assert "libfcitx5core" in metadata and "libc6" in metadata
        sums = []
        for line in (extracted / "DEBIAN/md5sums").read_text().splitlines():
            digest, path = line.split("  ", 1)
            assert path.startswith("usr/") and ".." not in Path(path).parts
            sums.append((digest, path))
        assert len(sums) == 5, sums

        run(dpkg + ["--install", str(package)])
        for digest, path in sums:
            assert hashlib.md5((root / path).read_bytes()).hexdigest() == digest
        run(dpkg + ["--install", str(package)]) # reinstall

        # Build an identical payload with a higher version to exercise upgrades.
        version = run(["dpkg-deb", "--field", str(package), "Version"]).strip()
        control = extracted / "DEBIAN/control"
        control.write_text(control.read_text().replace(f"Version: {version}\n", f"Version: {version}+test1\n"))
        upgrade = work / "upgrade.deb"
        run(["dpkg-deb", "--root-owner-group", "--build", str(extracted), str(upgrade)])
        run(dpkg + ["--install", str(upgrade)])
        run(dpkg + ["--install", str(package)]) # rollback
        run(dpkg + ["--remove", "fcitx5-chengyin"])
        assert all(not (root / path).exists() for _, path in sums)
        assert user_config.read_text() == "DictionaryPath=/home/test/private.tsv\n"
        run(dpkg + ["--purge", "fcitx5-chengyin"])
    print(f"{package.name}: isolated-root install/reinstall/upgrade/rollback/remove/purge passed")


if __name__ == "__main__":
    try:
        if len(sys.argv) < 2:
            raise SystemExit("Usage: test_deb.py package.deb [package.deb ...]")
        for argument in sys.argv[1:]:
            test_package(Path(argument).resolve())
    except subprocess.CalledProcessError as error:
        print(error.stdout, file=sys.stderr)
        print(error.stderr, file=sys.stderr)
        raise
