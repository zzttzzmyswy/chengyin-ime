#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Exercise the Arch package lifecycle in an isolated pacman root, never on the
# host. Runtime dependency checks are skipped on purpose: this validates the
# payload, file tracking and user-data preservation, not dependency resolution
# or a real desktop session.
set -euo pipefail

package=${1:?usage: test_arch_package.sh <package.pkg.tar.zst>}
package=$(realpath "$package")
root=$(mktemp -d /tmp/chengyin-arch-test-XXXXXX)
trap 'rm -rf "$root"' EXIT

db="$root/var/lib/pacman"
# --root/--dbpath keep every operation inside the throwaway tree. -dd, not -d: a
# single -d still requires the dependency packages to be present, which this
# file-only fixture deliberately does not install. Query mode rejects both.
pacman() { command pacman --root="$root" --dbpath="$db" --noconfirm "$@"; }
transaction() { pacman -dd "$@"; }

mkdir -p "$db" "$db/../cache/pacman/pkg" "$root/usr/lib/fcitx5"
# A user configuration and a private lexicon the package must never touch.
mkdir -p "$root/root/.config/fcitx5/conf"
printf 'DictionaryPath=/root/private.tsv\n' > "$root/root/.config/fcitx5/conf/chengyin.conf"
printf "ni'hao\t私有词\t9\n" > "$root/root/private.tsv"

echo "=== metadata ==="
command pacman -Qip "$package" | grep -E '^(Name|Version|Architecture|Depends On)'

echo "=== install ==="
transaction -U "$package" >/dev/null
pacman -Q fcitx5-chengyin

echo "=== payload ==="
for required in \
  usr/lib/fcitx5/chengyin.so \
  usr/share/chengyin/daily.tsv \
  usr/share/chengyin/licenses/LICENSE.jieba \
  usr/share/chengyin/licenses/SOURCE.rime-pinyin-simp.json \
  usr/share/fcitx5/addon/chengyin.conf \
  usr/share/fcitx5/inputmethod/chengyin.conf \
  usr/share/doc/fcitx5-chengyin/copyright ; do
  # pacman prefixes -Ql output with --root, so match the path tail, not an
  # anchored absolute path.
  pacman -Ql fcitx5-chengyin 2>/dev/null | grep -q "/$required$" || { echo "MISSING $required" >&2; exit 1; }
done
echo "all required files present"
if pacman -Ql fcitx5-chengyin 2>/dev/null | grep -qi 'Debian'; then
  echo "Arch package must not carry the Debian install notes" >&2; exit 1
fi

echo "=== reinstall ==="
transaction -U "$package" >/dev/null
pacman -Q fcitx5-chengyin

echo "=== upgrade to a higher pkgver ==="
# Repack the same payload with a higher version to exercise pacman's upgrade
# path; building a second makepkg tree here would only re-test makepkg itself.
upgrade=$(mktemp -d /tmp/chengyin-arch-upgrade-XXXXXX)/upgrade.pkg.tar.zst
repack=$(mktemp -d /tmp/chengyin-arch-repack-XXXXXX)
( cd "$repack" && bsdtar xf "$package" && sed -i 's/^pkgver = .*/pkgver = 0.1.0.preview99-1/' .PKGINFO && rm -f .MTREE \
  && bsdtar --format=gnutar -cf - .PKGINFO usr | zstd -q -o "$upgrade" )
transaction -U "$upgrade" >/dev/null
pacman -Q fcitx5-chengyin | grep -q '0.1.0.preview99-1' || { echo "upgrade did not apply" >&2; exit 1; }
echo "upgraded to $(pacman -Q fcitx5-chengyin)"
rm -rf "$repack"

echo "=== user data after reinstall ==="
grep -q 'DictionaryPath=/root/private.tsv' "$root/root/.config/fcitx5/conf/chengyin.conf"
grep -q "私有词" "$root/root/private.tsv"
echo "user config and private lexicon untouched"

echo "=== remove ==="
transaction -R fcitx5-chengyin >/dev/null
if pacman -Q fcitx5-chengyin >/dev/null 2>&1; then echo "package still installed" >&2; exit 1; fi
for required in usr/lib/fcitx5/chengyin.so usr/share/chengyin/daily.tsv usr/share/fcitx5/addon/chengyin.conf; do
  if [ -e "$root/$required" ]; then echo "left behind: $required" >&2; exit 1; fi
done
grep -q 'DictionaryPath=/root/private.tsv' "$root/root/.config/fcitx5/conf/chengyin.conf"
grep -q "私有词" "$root/root/private.tsv"
echo "package files removed; user config and lexicon kept"

echo "PASS: isolated pacman lifecycle (dependencies intentionally unchecked)"
