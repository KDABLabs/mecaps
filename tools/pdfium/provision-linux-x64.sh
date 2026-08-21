#!/usr/bin/env bash

set -euo pipefail

readonly PDFIUM_VERSION="153.0.8009.0"
readonly PDFIUM_REVISION="acf52a0b01420c97ed1005ae171edd63bd4701bd"
readonly ARCHIVE_NAME="pdfium-linux-x64.tgz"
readonly ARCHIVE_SHA256="be513e8021a5bf8eb2116e00d78c3bacb82c5a02b3785156ae14fe5e33084385"
readonly ARCHIVE_URL="https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F8009/${ARCHIVE_NAME}"

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <sdk-root-outside-mecaps>" >&2
    exit 2
fi

readonly script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly source_root="$(cd "${script_dir}/../.." && pwd)"
mkdir -p "$1"
readonly output_root="$(realpath "$1")"
readonly sdk_dir="${output_root}/pdfium-${PDFIUM_VERSION}-linux-x64"

case "${output_root}/" in
    "${source_root}/"*)
        echo "SDK output must be outside the mecaps source tree: ${source_root}" >&2
        exit 2
        ;;
esac

for tool in cmake curl sha256sum tar; do
    command -v "${tool}" >/dev/null || {
        echo "Required tool not found: ${tool}" >&2
        exit 2
    }
done

readonly work_dir="$(mktemp -d)"
trap 'rm -rf "${work_dir}"' EXIT
readonly archive="${work_dir}/${ARCHIVE_NAME}"
readonly staging_dir="${work_dir}/sdk"

curl --fail --location --retry 3 --output "${archive}" "${ARCHIVE_URL}"
echo "${ARCHIVE_SHA256}  ${archive}" | sha256sum --check --status

mkdir "${staging_dir}"
tar -xzf "${archive}" -C "${staging_dir}"

for required_file in LICENSE PDFiumConfig.cmake VERSION args.gn include/fpdfview.h lib/libpdfium.so licenses/pdfium.txt; do
    [[ -f "${staging_dir}/${required_file}" ]] || {
        echo "SDK is missing required file: ${required_file}" >&2
        exit 1
    }
done

grep -Fxq 'pdf_enable_v8 = false' "${staging_dir}/args.gn"
grep -Fxq 'pdf_enable_xfa = false' "${staging_dir}/args.gn"
grep -Fxq 'target_cpu = "x64"' "${staging_dir}/args.gn"
grep -Fxq 'target_os = "linux"' "${staging_dir}/args.gn"

cat >"${staging_dir}/MECAPS-PROVENANCE.txt" <<EOF
PDFium version: ${PDFIUM_VERSION}
PDFium revision: ${PDFIUM_REVISION}
Build recipe: https://github.com/bblanchon/pdfium-binaries/tree/chromium/8009
Build configuration: args.gn
Source archive: ${ARCHIVE_URL}
Source archive SHA-256: ${ARCHIVE_SHA256}
EOF

echo "${ARCHIVE_SHA256}  ${ARCHIVE_NAME}" >"${staging_dir}/SHA256SUMS"

cmake -S "${script_dir}/smoke" -B "${work_dir}/smoke-build" \
    -DPDFium_DIR="${staging_dir}"
cmake --build "${work_dir}/smoke-build"
ctest --test-dir "${work_dir}/smoke-build" --output-on-failure

rm -rf "${sdk_dir}"
mv "${staging_dir}" "${sdk_dir}"

echo "Provisioned PDFium host SDK: ${sdk_dir}"
