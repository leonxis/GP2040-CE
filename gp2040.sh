#!/usr/bin/env bash
# Build GP2040-CE firmware for KEYBOARD branch.
# Web UI is not shipped separately: CMake runs npm in www/ during configure and embeds the bundle.
set -euo pipefail

PROJECT_DIR="/home/leonxis/GP2040/GP2040-CE"
FW_BUILD_DIR="${PROJECT_DIR}/build"
FTP_DIR="/home/leonxis/FTP"

export GP2040_BOARDCONFIG="${GP2040_BOARDCONFIG:-HML}"

# CMakeLists reads GP2040_BOARDCONFIG from the environment (see GP2040-CE CMakeLists.txt).
if [[ -z "${PICO_SDK_PATH:-}" ]]; then
	if [[ -d "${HOME}/GP2040/pico-sdk" ]]; then
		export PICO_SDK_PATH="${HOME}/GP2040/pico-sdk"
	elif [[ -d "${PROJECT_DIR}/../pico-sdk" ]]; then
		export PICO_SDK_PATH="$(cd "${PROJECT_DIR}/../pico-sdk" && pwd)"
	fi
fi

JOBS="$(nproc 2>/dev/null || echo 4)"

# Pico SDK (tinyusb) builds pioasm via CMake ExternalProject into ${FW_BUILD_DIR}/pioasm.
# If that tree was removed or never generated but stamp files still mark steps complete,
# the build fails with make: no Makefile. Drop stale ExternalProject state so configure runs again.
pioasm_dir="${FW_BUILD_DIR}/pioasm"
pioasm_stamp_dir="${FW_BUILD_DIR}/pico-sdk/src/rp2_common/tinyusb/pioasm/src/pioasmBuild-stamp"
if [[ -d "${pioasm_dir}" ]] && [[ ! -f "${pioasm_dir}/Makefile" ]] && [[ ! -f "${pioasm_dir}/build.ninja" ]]; then
	echo "[warn] pioasm build dir has no Makefile (stale ExternalProject state); cleaning pioasm cache."
	rm -rf "${pioasm_dir}" "${FW_BUILD_DIR}/pioasm-install"
	if [[ -d "${pioasm_stamp_dir}" ]]; then
		rm -f "${pioasm_stamp_dir}"/pioasmBuild-*
	fi
fi

echo "[1/2] Configure & build firmware (www built by CMake unless SKIP_WEBBUILD=1)"
mkdir -p "${FW_BUILD_DIR}"
cmake -S "${PROJECT_DIR}" -B "${FW_BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${FW_BUILD_DIR}" --parallel "${JOBS}"

# UF2 is produced in the same Makefile recipe as the .elf link step. Make only tracks the
# .elf as the primary output: if the .elf is up-to-date but .uf2 was deleted, the recipe
# is skipped and UF2 is never recreated. Regenerate when missing.
recover_uf2_from_elf() {
	local picotool_bin="${FW_BUILD_DIR}/_deps/picotool/picotool"
	shopt -s nullglob
	local -a elf_files
	elf_files=("${FW_BUILD_DIR}/GP2040-CE_"*"_${GP2040_BOARDCONFIG}.elf")
	if [[ ${#elf_files[@]} -ne 1 ]] || [[ ! -x "${picotool_bin}" ]]; then
		return 1
	fi
	local elf="${elf_files[0]}"
	local base
	base=$(basename "${elf}" .elf)
	echo "[warn] UF2 missing after build; generating ${base}.uf2 with picotool (see GP2040-CE CMake make vs UF2 outputs)."
	"${picotool_bin}" uf2 convert "${elf}" "${FW_BUILD_DIR}/${base}.uf2" --family rp2040
}

shopt -s nullglob
uf2_list=("${FW_BUILD_DIR}/GP2040-CE_"*"_${GP2040_BOARDCONFIG}.uf2")
if [[ ${#uf2_list[@]} -eq 0 ]]; then
	if ! recover_uf2_from_elf; then
		echo "ERROR: No GP2040-CE_*_${GP2040_BOARDCONFIG}.uf2 in ${FW_BUILD_DIR} and could not run picotool fallback." >&2
		echo "  Set PICO_SDK_PATH to your pico-sdk clone (current: ${PICO_SDK_PATH:-unset})." >&2
		exit 1
	fi
	uf2_list=("${FW_BUILD_DIR}/GP2040-CE_"*"_${GP2040_BOARDCONFIG}.uf2")
fi
PRIMARY_UF2="${uf2_list[0]}"
echo "[1/2] Firmware UF2: ${PRIMARY_UF2}"

echo "[2/2] Copy firmware to FTP"
mkdir -p "${FTP_DIR}"
cp -f "${PRIMARY_UF2}" "${FTP_DIR}/GNS.uf2"

echo "Done:"
echo "  Firmware: ${FTP_DIR}/GNS.uf2"
