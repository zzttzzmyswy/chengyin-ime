#!/usr/bin/env python3
"""Destructive installer lifecycle tests: use a clean isolated Windows runner/Wine prefix."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import time
import threading
import struct
import zlib

from package_windows import build_installer

CLASS = r"Software\Classes\CLSID\{65C32A54-219A-4F0A-B44C-B963D7BA532F}\InprocServer32"
ARP = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\ChengyinIME"
# preview24 and earlier registered their uninstall entry under the former name.
LEGACY_ARP = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\MyswyIME"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--previous-package", type=Path, help="Optional preserved previous installer for actual version upgrade")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--makensis", default="makensis")
    parser.add_argument("--runner", help="Optional executable runner, e.g. a Wine wrapper")
    parser.add_argument("--wine-prefix", type=Path)
    args = parser.parse_args()
    if os.name != "nt" and not (args.runner and args.wine_prefix):
        parser.error("Non-Windows tests need both --runner and --wine-prefix")
    prefix = [args.runner] if args.runner else []
    driver = (args.build_dir / "Release/chengyin_probe.exe" if (args.build_dir / "Release").exists()
              else args.build_dir / "chengyin_probe.exe").resolve()

    def run(*command: str | Path, success: bool = True) -> subprocess.CompletedProcess:
        result = subprocess.run([*prefix, *map(str, command)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=45)
        if (result.returncode == 0) != success:
            raise AssertionError(f"Unexpected exit {result.returncode}: {command}\n{result.stdout.decode(errors='replace')}")
        return result

    def read(key: str, name: str = "") -> str | None:
        if os.name == "nt":
            import winreg
            try:
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key, 0,
                                    winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as handle:
                    return str(winreg.QueryValueEx(handle, name)[0])
            except FileNotFoundError:
                return None
        # reg.exe redirects using a legacy code page in Wine; Chinese names can
        # become question marks. Read the wide Win32 value and emit UTF-8 instead.
        result = subprocess.run([*prefix, str(driver), "--read-registry", key, name],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
        if result.returncode == 1:
            return None
        assert result.returncode == 0, f"Cannot read isolated registry value: {key} / {name}"
        return result.stdout.decode("utf-8")

    product_name = "澄音输入法（预览）"
    menu_root = None
    program_files = ((args.wine_prefix / "drive_c/Program Files") if args.wine_prefix
                     else Path(os.environ["ProgramFiles"]))
    product = program_files / "ChengyinIME"
    # preview24 and earlier installed under the former project name.
    legacy_product = program_files / "MyswyIME"

    def host_path(windows_path: str) -> Path:
        if args.wine_prefix:
            assert windows_path.startswith("C:\\")
            return args.wine_prefix / "drive_c" / windows_path[3:].replace("\\", "/")
        return Path(windows_path)

    def windows_path(path: Path) -> str:
        if args.wine_prefix:
            return "C:\\" + str(path.relative_to(args.wine_prefix / "drive_c")).replace("/", "\\")
        return str(path)

    menu_path = read(r"Software\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders", "Common Programs")
    assert menu_path, "Cannot locate the isolated Start menu"
    menu_root = host_path(menu_path)

    assert read(CLASS) is None and read(ARP, "InstallLocation") is None, "Use a clean isolated runner"
    assert not product.exists() or not any(product.iterdir()), "Existing product files must not be touched"

    def installed(version: str) -> Path:
        expected = product / version
        location = read(ARP, "InstallLocation")
        assert location and host_path(location) == expected, "Uninstall entry points at the wrong directory"
        assert read(CLASS) == location + "\\chengyin_tsf.dll", "COM path does not match install entry"
        assert (expected / "chengyin-install.txt").read_text().strip() == version
        actual_name = read(ARP, "DisplayName")
        assert actual_name == product_name, (
            f"Installed Apps product name mismatch: {ascii(actual_name)} != {ascii(product_name)}")
        if menu_root:
            menu = menu_root / "澄音输入法"
            assert all((menu / (name + ".lnk")).is_file() for name in ["设置", "使用说明", "卸载"]), "New Start menu actions missing"
            assert not (menu / "输入测试.lnk").exists(), "Standalone test shortcut must be removed"
            assert not (expected / "chengyin_testpad.exe").exists(), "Standalone test program must be removed"
        return expected

    def uninstall(folder: Path) -> None:
        # _?= disables the temporary child copy so CI can observe the actual exit
        # code. The normal Apps & Features entry uses NSIS's temporary copy.
        run(driver, "--uninstall-exe", folder / "Uninstall.exe")
        assert read(CLASS) is None and read(ARP, "InstallLocation") is None
        if menu_root:
            menu = menu_root / "澄音输入法"
            assert not any((menu / (name + ".lnk")).exists() for name in ["输入测试", "设置", "使用说明", "卸载"]), "Owned Start menu actions must be removed"

    with tempfile.TemporaryDirectory(prefix="chengyin-lifecycle-") as temporary:
        work = Path(temporary)
        current: Path | None = None
        settings_files: dict[Path, bytes] = {}
        menu_note: Path | None = None
        menu_note_bytes = b"keep this owned upgrade fixture\n"
        settings_bytes = b"ni\tlocal-vocabulary\t10\n"
        try:
            run((args.previous_package or args.package).resolve(), "/S")
            # A preserved preview24 installer registers under the former name.
            previous_location = read(ARP, "InstallLocation") or read(LEGACY_ARP, "InstallLocation")
            assert previous_location is not None
            current = host_path(previous_location)
            previous_is_legacy = read(ARP, "InstallLocation") is None
            if args.wine_prefix:
                local_data=run("cmd.exe","/c","set","LOCALAPPDATA").stdout.decode(errors="replace").strip()
                assert local_data.startswith("LOCALAPPDATA="), "Cannot locate isolated local settings"
                settings_root=host_path(local_data.split("=",1)[1]) / ("MyswyIME" if previous_is_legacy else "ChengyinIME")
            else:
                settings_root=Path(os.environ["LOCALAPPDATA"]) / ("MyswyIME" if previous_is_legacy else "ChengyinIME")
            # Only create owned fixtures after checking every user path first.
            learning_record = struct.pack("<HHII", 5, 6, 7, 1) + b"nihao" + "你好".encode("utf-8")
            learning_bytes = b"MSWYUSR1" + struct.pack("<III", 1, 1, zlib.crc32(learning_record)) + learning_record
            proposed = {
                settings_root / "dictionary.custom": settings_bytes,
                settings_root / "preferences.ini": b"\xff\xfe" + "Version=1\nFont=Microsoft YaHei UI\nFontSize=18\nPageSize=7\n".encode("utf-16le"),
                settings_root / "learning.profile": learning_bytes,
            }
            assert all(not path.exists() for path in proposed), "Use a clean runner without existing user data"
            settings_root.mkdir(parents=True, exist_ok=True)
            for path, content in proposed.items():
                with path.open("xb") as owned_settings:
                    owned_settings.write(content)
                settings_files[path] = content

            if args.previous_package:
                previous_directory = current
                previous_info = json.loads((current / "BUILD_INFO.json").read_text(encoding="utf-8"))
                previous_revision = int(previous_info["version"].rsplit("-preview",1)[1])
                # Revisions 1..5 shipped the former-name Start menu folder; a
                # former-name previous install keeps it until it is migrated.
                old_menu = menu_root / (("Myswy 全拼" if previous_is_legacy else "Chengyin 全拼")
                                        if previous_revision < 6 else "澄音输入法")
                menu_note = old_menu / "user-added.txt"
                assert old_menu.is_dir() and not menu_note.exists()
                menu_note.write_bytes(menu_note_bytes)
                run(args.package.resolve(), "/S")
                location = read(ARP, "InstallLocation")
                assert location is not None
                current = host_path(location)
                assert current != previous_directory, "Previous installer must have a lower revision"
                previous_testpad = "myswy_testpad.exe" if previous_is_legacy else "chengyin_testpad.exe"
                assert not (previous_directory / previous_testpad).exists(), "Old standalone test executable must be removed"
                assert not previous_directory.exists() or not any(previous_directory.iterdir()), "Old install directory must be cleaned"
                if previous_is_legacy:
                    assert read(LEGACY_ARP, "InstallLocation") is None, "Obsolete uninstall key must be removed"
                if menu_root:
                    assert not (old_menu / "输入测试.lnk").exists(), "Old test shortcut must be removed"
                    if previous_revision < 6:
                        assert not any((old_menu / (name + ".lnk")).exists() for name in ["设置", "使用说明", "卸载"]), "Old product shortcuts must be migrated"
                    assert menu_note.read_bytes() == menu_note_bytes, "Unknown old Start menu files must be preserved"
                assert all(path.read_bytes() == content for path, content in settings_files.items()), "Actual previous-version upgrade preserves all user data"
                print("PASS: preserved previous EXE upgrades to new EXE; vocabulary/settings/learning preserved", flush=True)

            info = json.loads((current / "BUILD_INFO.json").read_text(encoding="utf-8"))
            version = info["version"]
            base, revision_text = version.rsplit("-preview", 1)
            revision = int(revision_text)
            installed(version)
            probe = current / "chengyin_probe.exe"
            run(probe, current / "chengyin_tsf.dll", "--registered")
            run(probe, "--verify-files", current, current / "SHA256SUMS.txt")
            run(args.package.resolve(), "/S")
            installed(version)
            # Same-version repair also recreates a missing COM class without
            # replacing files or changing the default input method.
            run("reg.exe", "delete", "HKLM\\" + CLASS.rsplit("\\", 1)[0], "/f")
            run(args.package.resolve(), "/S")
            installed(version)
            print("PASS: install, complete hashes, registered COM, same-version repair", flush=True)

            # A changed installed file prevents a misleading same-version success.
            readme = current / "README.md"
            original = readme.read_bytes()
            readme.write_bytes(original + b"\nmodified fixture\n")
            try:
                run(args.package.resolve(), "/S", success=False)
                installed(version)
            finally:
                readme.write_bytes(original)

            # Build complete upgrade and failure fixtures using the same packager.
            stage = work / "payload"
            stage.mkdir()
            for name in [line[66:] for line in (current / "SHA256SUMS.txt").read_text().splitlines()]:
                shutil.copy2(current / name, stage / name)

            def fixture(name: str, next_revision: int, bad: bool = False) -> Path:
                next_version = f"{base}-preview{next_revision}"
                updated = dict(info, version=next_version)
                (stage / "BUILD_INFO.json").write_text(json.dumps(updated) + "\n", encoding="utf-8")
                dll = (args.build_dir / "Release/chengyin_registration_failure.dll"
                       if (args.build_dir / "Release").exists()
                       else args.build_dir / "chengyin_registration_failure.dll") if bad else current / "chengyin_tsf.dll"
                shutil.copy2(dll, stage / "chengyin_tsf.dll")
                hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest().upper()
                          for p in sorted(stage.iterdir()) if p.name != "SHA256SUMS.txt"}
                (stage / "SHA256SUMS.txt").write_text("".join(f"{h}  {n}\n" for n, h in hashes.items()), encoding="ascii")
                exe = work / (name + ".exe")
                build_installer(stage, exe, args.makensis, next_version, next_revision)
                return exe

            failure = fixture("fault-fixture", revision + 1, bad=True)
            run(failure, "/S", success=False)
            installed(version)
            run(probe, current / "chengyin_tsf.dll", "--registered")
            assert not (product / f"{base}-preview{revision + 1}").exists()
            print("PASS: injected upgrade registration failure restores old COM and files", flush=True)

            # An unrecognized registration must prevent both uninstall and upgrade.
            original_server = read(CLASS)
            run("reg.exe", "add", "HKLM\\" + CLASS, "/ve", "/t", "REG_SZ", "/d", r"C:\foreign\tip.dll", "/f")
            try:
                run(driver, "--uninstall-exe", current / "Uninstall.exe", success=False)
                run(failure, "/S", success=False)
                assert read(CLASS) == r"C:\foreign\tip.dll" and (current / "chengyin_tsf.dll").exists()
            finally:
                run("reg.exe", "add", "HKLM\\" + CLASS, "/ve", "/t", "REG_SZ", "/d", original_server, "/f")
            print("PASS: changed files and foreign registration are preserved", flush=True)

            unknown = current / "user-added.txt"
            unknown.write_text("keep", encoding="utf-8")
            upgrade = fixture("upgrade-fixture", revision + 1)
            old = current
            stop_event = "Local\\Chengyin.IM.TestHold." + work.name
            held = subprocess.Popen([*prefix, str(driver), "--hold-dll", str((old / "chengyin_tsf.dll").resolve()), stop_event],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            ready: queue.Queue = queue.Queue()
            threading.Thread(target=lambda: ready.put(held.stdout.readline()), daemon=True).start()
            try:
                assert ready.get(timeout=15).strip() == b"READY", "DLL holder did not start"
                run(upgrade, "/S")
                current = installed(f"{base}-preview{revision + 1}")
                if (old / "chengyin_tsf.dll").exists():
                    if os.name == "nt":
                        import winreg
                        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Control\Session Manager") as key:
                            pending = "\n".join(winreg.QueryValueEx(key, "PendingFileRenameOperations")[0])
                    else:
                        pending = run("reg.exe", "query", r"HKLM\SYSTEM\CurrentControlSet\Control\Session Manager",
                                      "/v", "PendingFileRenameOperations").stdout.decode(errors="replace")
                    assert "ChengyinIME" in pending and "chengyin_tsf.dll" in pending, "Occupied old DLL must be queued for reboot cleanup"
            finally:
                if held.poll() is None:
                    run(driver, "--release-dll-hold", stop_event)
                assert held.wait(timeout=15) == 0, "DLL holder did not shut down"
                held.stdout.close()
            current = installed(f"{base}-preview{revision + 1}")
            assert unknown.read_text() == "keep"
            # Once the holder exits, CI can remove its owned deferred DLL;
            # production Windows performs the queued removal at reboot.
            (old / "chengyin_tsf.dll").unlink(missing_ok=True)
            run(args.package.resolve(), "/S", success=False)
            installed(f"{base}-preview{revision + 1}")
            assert all(path.read_bytes() == content for path, content in settings_files.items()), "Upgrade must preserve vocabulary, preferences and learning"
            print("PASS: upgrade with DLL loaded, version path switch, downgrade refusal, unknown file preservation", flush=True)

            # Keep an external copy for idempotent repeated cleanup. This tests
            # fixed owned paths, independently of NSIS's temporary-copy location.
            second_uninstaller = work / "cleanup.exe"
            shutil.copy2(current / "Uninstall.exe", second_uninstaller)
            user_file = current / "my-custom.tsv"
            user_file.write_text("user data", encoding="utf-8")
            uninstall(current)
            run(driver, "--uninstall-exe", second_uninstaller)
            assert read(CLASS) is None and read(ARP, "InstallLocation") is None
            assert user_file.read_text() == "user data"
            assert all(path.read_bytes() == content for path, content in settings_files.items()), "Uninstall must preserve vocabulary, preferences and learning"
            user_file.unlink()
            unknown.unlink()
            old.rmdir()
            current = None
            # A failed fresh install must leave neither registration nor payload.
            run(failure, "/S", success=False)
            assert read(CLASS) is None and read(ARP, "InstallLocation") is None
            print("PASS: uninstall, repeated cleanup, failed fresh registration rollback", flush=True)

            # Emulate the earlier ZIP's installed layout: no Apps & Features
            # entry and an ASCII/CRLF marker. No PowerShell runtime is needed.
            legacy = legacy_product / "0.1.0-preview1"
            legacy.mkdir(parents=True)
            for name in ("myswy_tsf.dll", "myswy_probe.exe", "myswy_testpad.exe", "README.md", "LICENSE",
                         "THIRD_PARTY.md", "RUNTIME_LICENSES.zip", "BUILD_INFO.json"):
                source = stage / ("chengyin_settings.exe" if name == "myswy_testpad.exe" else name)
                if name == "myswy_tsf.dll":
                    source = args.build_dir / ("Release/chengyin_tsf.dll" if (args.build_dir / "Release").exists() else "chengyin_tsf.dll")
                shutil.copy2(source, legacy / name)
            (legacy / "myswy-install.txt").write_bytes(b"0.1.0-preview1\r\n")
            (legacy / "BUILD_INFO.json").write_text(json.dumps(dict(info, version="0.1.0-preview1")), encoding="utf-8")
            for name in ("Install.ps1", "Uninstall.ps1", "SHA256SUMS.json"):
                (legacy / name).write_text("# legacy layout fixture\n", encoding="utf-8")
            run("regsvr32.exe", "/s", windows_path(legacy / "myswy_tsf.dll"))
            run(args.package.resolve(), "/S")
            current = installed(version)
            assert not legacy.exists()
            assert not legacy_product.exists()
            # Exercise the ordinary temporary-copy uninstaller used by Apps &
            # Features, in addition to the deterministic exit-code tests above.
            run(current / "Uninstall.exe", "/S")
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if read(CLASS) is None and read(ARP, "InstallLocation") is None and not current.exists():
                    break
                time.sleep(0.1)
            else:
                raise AssertionError("Normal temporary-copy uninstall did not finish")
            current = None
            print("PASS: legacy ZIP layout migration and normal Apps & Features uninstaller", flush=True)
        finally:
            if menu_note and menu_note.exists() and menu_note.read_bytes() == menu_note_bytes:
                menu_note.unlink()
                if not any(menu_note.parent.iterdir()):
                    menu_note.parent.rmdir()
            for path, content in settings_files.items():
                if path.exists() and path.read_bytes() == content:
                    path.unlink()
            if settings_files and not any(settings_root.iterdir()):
                settings_root.rmdir()
            if current and (current / "Uninstall.exe").exists():
                location = read(ARP, "InstallLocation")
                if location and host_path(location) == current and read(CLASS) == location + "\\chengyin_tsf.dll":
                    uninstall(current)
            # _?= leaves a running uninstaller scheduled for deletion. Once the
            # test process exits it can be removed locally without rebooting CI.
            if product.exists():
                for folder in product.iterdir():
                    if folder.is_dir() and not folder.is_symlink() and folder.name.startswith("0.1.0-preview"):
                        remaining = list(folder.iterdir())
                        if all(p.name == "Uninstall.exe" for p in remaining):
                            for p in remaining:
                                p.unlink()
                            folder.rmdir()
                if not any(product.iterdir()):
                    product.rmdir()
            if legacy_product.exists():
                for folder in legacy_product.iterdir():
                    if folder.is_dir() and not folder.is_symlink() and folder.name.startswith("0.1.0-preview"):
                        remaining = list(folder.iterdir())
                        if all(p.name == "Uninstall.exe" for p in remaining):
                            for p in remaining:
                                p.unlink()
                            folder.rmdir()
                if not any(legacy_product.iterdir()):
                    legacy_product.rmdir()


if __name__ == "__main__":
    main()
