#!/bin/bash
# Train the committed MD/MM profile (lever L6, doc/modern-ux/RESEARCH-emulation-cpu.md "L6 measured"): on this Mac,
# with your ROMs, never in CI. Builds mdmmPerfGateTest instrumented (the release configuration: Release, ThinLTO,
# the DSP libraries, this Mac's architecture), runs every scenario of both machines (stopped, then playing, speed-ups
# as shipped), merges the counts, writes them as LLVM's text profile and records what they were trained on:
#
#   source/elektron/md/pgo/mdmm-macos.proftext   function names, hashes and execution counts (no firmware)
#   source/elektron/md/pgo/mdmm-macos.json       commit, git tree of each profiled folder (the staleness check),
#                                                compiler, workload, the ROMs' SHA-256
#
# Before it writes them it proves the profile is profile data only (scripts/macos/mdmm_pgo_profile.py check: every
# line parses as LLVM's text format, and no run of 31 bytes of either ROM is in the file). Commit both files.
#
#   scripts/macos/train_mdmm_pgo.sh [build-dir]     (default temp/pgo-train; kept, so a retrain is incremental)
#
# Environment: GEARMULATOR_MD_FIRMWARE_BIN, GEARMULATOR_MM_FIRMWARE_BIN (default: the ROMs in
# ~/Documents/Gearmulator Preview/<machine>/roms), MDMM_PGO_SECONDS (per phase, default 8), MDMM_PGO_JOBS.
# The runs are headless (no audio device) and use a scratch data root; commit the profiled folders first (the
# record names their committed trees).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "${here}/../.." && pwd)"
build="${1:-${root}/temp/pgo-train}"
seconds="${MDMM_PGO_SECONDS:-8}"
jobs="${MDMM_PGO_JOBS:-$(sysctl -n hw.ncpu)}"
pgo="${root}/source/elektron/md/pgo"
rom_dir="${HOME}/Documents/Gearmulator Preview"
md_rom="${GEARMULATOR_MD_FIRMWARE_BIN:-$(ls "${rom_dir}/Machinedrum/roms/"*.bin 2>/dev/null | head -n 1)}"
mm_rom="${GEARMULATOR_MM_FIRMWARE_BIN:-$(ls "${rom_dir}/Monomachine/roms/"*.bin 2>/dev/null | head -n 1)}"
[ -f "${md_rom}" ] || { echo "No Machinedrum ROM (GEARMULATOR_MD_FIRMWARE_BIN)" >&2; exit 2; }
[ -f "${mm_rom}" ] || { echo "No Monomachine ROM (GEARMULATOR_MM_FIRMWARE_BIN)" >&2; exit 2; }
arch="$(uname -m)"
SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
export SDKROOT

cmake -S "${root}" -B "${build}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="${arch}" \
	-DCMAKE_OSX_DEPLOYMENT_TARGET=10.13 -DCMAKE_OSX_SYSROOT="${SDKROOT}" -DXCODE_VERSION="${XCODE_VERSION:-16}" \
	-DGEARMULATOR_MDMM_APPLE_THINLTO=ON -DGEARMULATOR_MDMM_APPLE_OPTIMIZE_DSP=ON \
	-DGEARMULATOR_MDMM_APPLE_PGO_MODE=generate -DGEARMULATOR_MDMM_APPLE_PGO_PROFILE= \
	-DBUILD_TESTING=ON -Dgearmulator_BUILD_JUCEPLUGIN=OFF > /dev/null
cmake --build "${build}" --parallel "${jobs}" --target mdmmPerfGateTest
tool="${build}/source/elektron/md/mdLibTest/mdmmPerfGateTest"

work="${build}/pgo-run"
rm -rf "${work}"
mkdir -p "${work}/raw" "${work}/data" "${work}/logs"
workload=()
pids=()
for run in md:md-busy md:md-factory md:md-song mm:mm-a01 mm:mm-busy mm:mm-song; do
	machine="${run%%:*}"; scenario="${run#*:}"
	rom="${md_rom}"; [ "${machine}" = mm ] && rom="${mm_rom}"
	workload+=("mdmmPerfGateTest ${machine} ${seconds} --scenario ${scenario} --outputs all (stopped, then playing)")
	LLVM_PROFILE_FILE="${work}/raw/${scenario}-%p.profraw" GEARMULATOR_DATA_ROOT="${work}/data/" \
		"${tool}" "${rom}" "${machine}" "${seconds}" --scenario "${scenario}" --outputs all \
		> "${work}/logs/${scenario}.log" 2>&1 &
	pids+=($!)
done
for pid in "${pids[@]}"; do
	wait "${pid}" || { echo "A training run failed: ${work}/logs" >&2; exit 1; }
done
rm -rf "${work}/data"

xcrun llvm-profdata merge --sparse -o "${work}/merged.profdata" "${work}/raw/"*.profraw
xcrun llvm-profdata merge --sparse --text -o "${work}/mdmm-macos.proftext" "${work}/merged.profdata"
python3 -B "${here}/mdmm_pgo_profile.py" check "${work}/mdmm-macos.proftext" --rom "${md_rom}" --rom "${mm_rom}"

mkdir -p "${pgo}"
cp "${work}/mdmm-macos.proftext" "${pgo}/mdmm-macos.proftext"
python3 -B "${here}/mdmm_pgo_profile.py" record "${pgo}/mdmm-macos.proftext" "${pgo}/mdmm-macos.json" \
	--source "${root}" --architecture "${arch}" \
	--compiler "$(xcrun clang --version | head -n 1)" \
	"${workload[@]/#/--workload=}" --rom "MD=${md_rom}" --rom "MM=${mm_rom}"
echo "Profile written: ${pgo}/mdmm-macos.proftext and mdmm-macos.json; commit both."
