#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
haxeon_dir=${HAXEON_DIR:-"$(dirname "$repo_dir")/realtime-haxe"}
runs=${NATIVEKIT_HAXEON_PROFILE_RUNS:-3}
allocation_interval=${NATIVEKIT_HAXEON_PROFILE_ALLOC_INTERVAL:-65536}
output_root=${NATIVEKIT_HAXEON_PROFILE_DIR:-"$repo_dir/out/haxeon-showcase-profile"}

usage() {
	echo "Usage: $0 [--runs N] [--output-dir DIR] [--haxeon-dir DIR]" >&2
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--runs)
			[[ $# -ge 2 && $2 =~ ^[1-9][0-9]*$ ]] || { usage; exit 2; }
			runs=$2
			shift 2
			;;
		--output-dir)
			[[ $# -ge 2 ]] || { usage; exit 2; }
			output_root=$2
			shift 2
			;;
		--haxeon-dir)
			[[ $# -ge 2 ]] || { usage; exit 2; }
			haxeon_dir=$2
			shift 2
			;;
		*)
			usage
			exit 2
			;;
	esac
done

[[ "$runs" =~ ^[1-9][0-9]*$ ]] || { usage; exit 2; }
[[ "$allocation_interval" =~ ^[1-9][0-9]*$ ]] || {
	echo "NATIVEKIT_HAXEON_PROFILE_ALLOC_INTERVAL must be a positive integer" >&2
	exit 2
}

module_dir="$repo_dir/modules/ui"
showcase_script="$module_dir/tools/showcase-wasm.sh"
profiler="$haxeon_dir/.tools/hashlink/hlprof-live"
contract=${NATIVEKIT_HAXEON_MEMORY_CONTRACT:-}
if [[ -z "$contract" ]]; then
	for candidate in \
		"$repo_dir/build-web/modules/ui/nativekit_haxeon_memory_contract.json" \
		"$repo_dir/build-wasm/nativekit_haxeon_memory_contract.json"; do
		if [[ -f "$candidate" ]]; then
			contract=$candidate
			break
		fi
	done
fi

if [[ ! -x "$haxeon_dir/scripts/build-native.sh" || ! -f "$haxeon_dir/bootstrap/compiler.hl" ]]; then
	echo "Haxeon build scripts or self-hosted compiler are missing at $haxeon_dir" >&2
	exit 1
fi
if [[ ! -f "$showcase_script" ]]; then
	echo "NativeKit Showcase build script is missing" >&2
	exit 1
fi
if [[ ! -f "$contract" ]]; then
	echo "NativeKit WASM memory contract is missing; configure an Emscripten UI build first" >&2
	exit 1
fi
(cd "$haxeon_dir" && ./scripts/build-native.sh >/dev/null)
if [[ ! -x "$profiler" || ! -x "$haxeon_dir/.tools/hashlink/hl" ]]; then
	echo "Haxeon native runtime or hlprof-live did not build" >&2
	exit 1
fi

if [[ "$output_root" != /* ]]; then
	output_root="$repo_dir/$output_root"
fi
mkdir -p "$output_root"
stamp=$(date -u +%Y%m%dT%H%M%SZ)
result_dir=$(mktemp -d "$output_root/${stamp}-XXXXXX")
profile_path="$result_dir/showcase.hlpc"
profile_report="$result_dir/profile-report.txt"
compiler_module=${NATIVEKIT_HAXEON_COMPILER_MODULE:-"$haxeon_dir/bootstrap/compiler.hl"}
if [[ ! -f "$compiler_module" ]]; then
	echo "Haxeon compiler module is missing: $compiler_module" >&2
	exit 1
fi
export LD_LIBRARY_PATH="$haxeon_dir/out:$haxeon_dir/.tools/hashlink${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

for index in $(seq 1 "$runs"); do
	run_dir="$result_dir/run-$index"
	mkdir "$run_dir"
	NATIVEKIT_WASM_BUILD_DIR="$run_dir" \
	NATIVEKIT_HAXEON_MEMORY_CONTRACT="$contract" \
	NATIVEKIT_HAXEON_SELF_HOSTED=1 \
	NATIVEKIT_HAXEON_SKIP_HXI_CHECK=1 \
	NATIVEKIT_HAXEON_TIME_FILE="$run_dir/time.txt" \
	NATIVEKIT_HAXEON_COMPILER_MODULE="$compiler_module" \
		"$showcase_script" >"$run_dir/build.log" 2>&1
done

profile_dir="$result_dir/profiled"
mkdir "$profile_dir"
port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
NATIVEKIT_WASM_BUILD_DIR="$profile_dir" \
NATIVEKIT_HAXEON_MEMORY_CONTRACT="$contract" \
NATIVEKIT_HAXEON_SELF_HOSTED=1 \
NATIVEKIT_HAXEON_SKIP_HXI_CHECK=1 \
NATIVEKIT_HAXEON_COMPILER_MODULE="$compiler_module" \
NATIVEKIT_HAXEON_DIAGNOSTICS_PORT="$port" \
	NATIVEKIT_HAXEON_TIME_FILE="$profile_dir/time.txt" \
	"$showcase_script" >"$profile_dir/build.log" 2>&1 &
compiler_pid=$!
cleanup() {
	kill "$compiler_pid" 2>/dev/null || true
	wait "$compiler_pid" 2>/dev/null || true
	if [[ -n ${profiler_pid:-} ]]; then
		kill "$profiler_pid" 2>/dev/null || true
		wait "$profiler_pid" 2>/dev/null || true
	fi
}
trap cleanup EXIT
"$profiler" --connect-timeout 15 --rate 1000 --alloc-interval "$allocation_interval" --interval 10000 --top 40 \
	--output "$profile_path" "$port" >"$result_dir/profiler.log" 2>&1 &
profiler_pid=$!
wait "$profiler_pid"
profiler_pid=
wait "$compiler_pid"
compiler_pid=
trap - EXIT

for wasm in "$result_dir"/run-*/nativekit_ui_showcase_wasm32.wasm "$profile_dir/nativekit_ui_showcase_wasm32.wasm"; do
	[[ -f "$wasm" ]] || { echo "Showcase artifact missing: $wasm" >&2; exit 1; }
done
first_artifact="$result_dir/run-1/nativekit_ui_showcase_wasm32.wasm"
for wasm in "$result_dir"/run-*/nativekit_ui_showcase_wasm32.wasm "$profile_dir/nativekit_ui_showcase_wasm32.wasm"; do
	cmp "$first_artifact" "$wasm"
done
"$profiler" report --top 40 "$profile_path" >"$profile_report" 2>"$result_dir/profile-report.err"
if [[ -s "$result_dir/profile-report.err" ]]; then
	cat "$result_dir/profile-report.err" >&2
	echo "Profiler capture did not finalize cleanly" >&2
	exit 1
fi

python3 - "$repo_dir" "$haxeon_dir" "$result_dir" "$runs" "$profile_path" "$profile_report" "$compiler_module" <<'PY'
import hashlib
import json
import pathlib
import subprocess
import sys
from datetime import datetime, timezone

repo = pathlib.Path(sys.argv[1])
haxeon = pathlib.Path(sys.argv[2])
result = pathlib.Path(sys.argv[3])
count = int(sys.argv[4])
profile = pathlib.Path(sys.argv[5])
report = pathlib.Path(sys.argv[6])
compiler_module = pathlib.Path(sys.argv[7]).resolve()

def git_info(path):
    revision = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=True, capture_output=True, text=True
    ).stdout.strip()
    dirty = bool(subprocess.run(
        ["git", "-C", str(path), "status", "--porcelain"],
        check=True, capture_output=True, text=True
    ).stdout)
    return {"revision": revision, "dirty": dirty}

def timed_run(path):
    values = {}
    for item in path.read_text().split():
        key, value = item.split("=", 1)
        values[key] = float(value) if key != "maxrss_kb" else int(value)
    return {
        "wall_seconds": values["wall"],
        "user_seconds": values["user"],
        "system_seconds": values["sys"],
        "peak_rss_kib": values["maxrss_kb"],
    }

def digest(path):
    result_hash = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            result_hash.update(chunk)
    return result_hash.hexdigest()

timings = []
hashes = []
for index in range(1, count + 1):
    run_dir = result / f"run-{index}"
    artifact = run_dir / "nativekit_ui_showcase_wasm32.wasm"
    timings.append({"run": index, **timed_run(run_dir / "time.txt")})
    hashes.append(digest(artifact))

profiled_dir = result / "profiled"
profiled_artifact = profiled_dir / "nativekit_ui_showcase_wasm32.wasm"
profiled = timed_run(profiled_dir / "time.txt")
profiled["artifact_sha256"] = digest(profiled_artifact)
profiled["profile_path"] = str(profile)
profiled["profile_report_path"] = str(report)

metadata = {
	"created_utc": datetime.now(timezone.utc).isoformat(),
	"nativekit": git_info(repo),
	"haxeon": git_info(haxeon),
	"compiler_module": str(compiler_module),
	"compiler_module_sha256": digest(compiler_module),
	"standalone_hxi_audits_skipped": True,
    "timed_runs": timings,
    "artifact_sha256": hashes,
    "profiled_run": profiled,
}
(result / "results.json").write_text(json.dumps(metadata, indent=2) + "\n")
print(f"Results: {result / 'results.json'}")
for run, artifact_hash in zip(timings, hashes):
    print(f"run {run['run']}: {run['wall_seconds']:.3f}s, {run['peak_rss_kib']} KiB peak RSS, sha256 {artifact_hash}")
print(f"profiled run: {profiled['wall_seconds']:.3f}s, capture {profile}")
print(f"Profile report: {report}")
PY
