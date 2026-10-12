#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Alternating A/B latency comparison for I24 (and any later hot-path iteration).
#
# The point of *alternating* two revisions on one machine is that it removes the
# thing a single before/after pair cannot: machine drift. This box is shared, so a
# single round can be off by ±20% (the reading I22's review adopted); alternating
# makes both revisions see the same drift, and reading the result as "is the P99
# median better *and* are the intervals disjoint" is what the acceptance criteria
# ask for.
#
# Usage:
#   scripts/bench_ab.sh <baseline-rev> [rounds] [rounds-count] [scenario...]
#
#   <baseline-rev>   git revision to build the A side from, e.g. origin/main
#   rounds           CHENGYIN_BENCH_ROUNDS per run (default 300)
#   rounds-count     alternations per side (default 4)
#   scenario...      scenario names for latency_scenarios (default: all)
#
# Each side is built in its own `git worktree` under build/i24-ab/, so neither the
# baseline nor the candidate can be contaminated by the other's target directory
# or by uncommitted edits in the working tree. The example and the prefix corpus
# are copied into the baseline worktree because they do not exist there.
#
# Output: build/i24-ab/<rev>/round-<n>.txt plus a median table on stdout. The
# digest of the corpus is printed so a reader can tell which corpus produced it.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
baseline_rev="${1:?usage: bench_ab.sh <baseline-rev> [rounds] [rounds-count] [scenario...]}"
rounds="${2:-300}"
alternations="${3:-4}"
shift 3 2>/dev/null || shift $#
scenarios=("$@")
if [ "${#scenarios[@]}" -eq 0 ]; then
    scenarios=(target-correction target-daily corpus-prefix)
fi

# Where the A/B artifacts land. Overridable so a re-run can keep its own copy
# rather than overwrite the run whose numbers are quoted in the delivery.
ab_root="${CHENGYIN_AB_OUT:-$repo/build/i24-ab}"
mkdir -p "$ab_root"
baseline_dir="$ab_root/baseline"
candidate_dir="$repo"

echo "== I24 A/B =="
echo "repo=$repo"
echo "baseline=$baseline_rev candidate=HEAD (working tree)"
echo "rounds=$rounds alternations=$alternations sweep_passes=${CHENGYIN_SWEEP_PASSES:-8} scenarios=${scenarios[*]}"
echo "prefix corpus sha256: $(sha256sum "$repo/data/eval/prefix.tsv" | cut -d' ' -f1)"

if [ ! -d "$baseline_dir" ]; then
    git -C "$repo" worktree add --detach "$baseline_dir" "$baseline_rev" >/dev/null
fi
mkdir -p "$ab_root/baseline" "$ab_root/candidate"
# The measurement harness is not part of the baseline revision: copy it in.
mkdir -p "$baseline_dir/crates/ime-core/examples"
cp "$repo/crates/ime-core/examples/latency_scenarios.rs" \
   "$baseline_dir/crates/ime-core/examples/"
cp "$repo/data/eval/prefix.tsv" "$baseline_dir/data/eval/"

echo "building baseline ..."
cargo build --release --manifest-path "$baseline_dir/Cargo.toml" \
    -p chengyin-core --example latency_scenarios --locked >/dev/null
echo "building candidate ..."
cargo build --release --manifest-path "$repo/Cargo.toml" \
    -p chengyin-core --example latency_scenarios --locked >/dev/null

baseline_bin="$baseline_dir/target/release/examples/latency_scenarios"
candidate_bin="$repo/target/release/examples/latency_scenarios"

# Alternation, not "all baseline then all candidate": the rounds must interleave so
# that a slow patch of machine time lands on both sides. Running one side's rounds
# as a block attributes machine drift to the revision, which is the failure mode
# this whole script exists to avoid — an earlier version of it did exactly that and
# reported a +26% "regression" between two byte-identical builds.
for scenario in "${scenarios[@]}"; do
    for round in $(seq 1 "$alternations"); do
        for side in baseline candidate; do
            if [ "$side" = baseline ]; then bin="$baseline_bin"; else bin="$candidate_bin"; fi
            out="$ab_root/$side/$scenario-round$round.txt"
            ( cd "$repo" && CHENGYIN_BENCH_ROUNDS="$rounds" \
                "$bin" "$scenario" > "$out" )
        done
    done
done

# Report the P99 median and range per scenario and side, plus the disjointness
# verdict the acceptance criteria rest on.
python3 - "$ab_root" "$alternations" "${scenarios[@]}" <<'PY'
import re
import statistics
import sys
from pathlib import Path

root = Path(sys.argv[1])
alternations = int(sys.argv[2])
scenarios = sys.argv[3:]
pattern = re.compile(r"p50=(\d+)ns p95=(\d+)ns p99=(\d+)ns")

def collect(side, scenario, metric):
    values = []
    for round in range(1, alternations + 1):
        text = (root / side / f"{scenario}-round{round}.txt").read_text()
        for line in text.splitlines():
            match = pattern.search(line)
            if match:
                values.append(int(match.group(metric)))
    return values

print(f"\n{'scenario':<20}{'side':<11}{'p50 med':>12}{'p99 med':>12}{'p99 min':>12}{'p99 max':>12}")
table = {}
for scenario in scenarios:
    for side in ("baseline", "candidate"):
        p50 = collect(side, scenario, 1)
        p99 = collect(side, scenario, 3)
        table[(scenario, side)] = (p50, p99)
        print(f"{scenario:<20}{side:<11}{statistics.median(p50):>12.0f}{statistics.median(p99):>12.0f}"
              f"{min(p99):>12}{max(p99):>12}")

print("\n== verdict (acceptance criterion 5) ==")
for scenario in scenarios:
    base_p99 = table[(scenario, "baseline")][1]
    cand_p99 = table[(scenario, "candidate")][1]
    base_med, cand_med = statistics.median(base_p99), statistics.median(cand_p99)
    disjoint = max(min(base_p99), min(cand_p99)) > min(max(base_p99), max(cand_p99))
    change = 100.0 * (cand_med - base_med) / base_med
    if disjoint and cand_med < base_med:
        verdict = "improved, intervals disjoint"
    elif change > 3.0:
        verdict = f"REGRESSED {change:+.1f}% (median), above the 3% bar"
    else:
        verdict = f"no decidable change ({change:+.1f}% median, intervals overlap)"
    print(f"  {scenario:<20} p99 med {base_med:.0f} -> {cand_med:.0f} ns ({change:+.1f}%)  {verdict}")
PY
