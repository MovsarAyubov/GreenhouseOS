#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="/workspace"
MAKEFILE_PATH="${PROJECT_DIR}/Debug/makefile"

normalize_makefile_linker_path() {
  if [[ ! -f "${MAKEFILE_PATH}" ]]; then
    echo "Error: ${MAKEFILE_PATH} not found."
    exit 1
  fi

  sed -i \
    -e 's#C:\\Users\\AUTcomp\\STM32CubeIDE\\workspace_1\.17\.0\\greenhouseOS\\STM32F407VETX_FLASH\.ld#../STM32F407VETX_FLASH.ld#g' \
    "${MAKEFILE_PATH}"
}

run_build() {
  normalize_makefile_linker_path
  make -C "${PROJECT_DIR}/Debug" -j"$(nproc)"
}

run_clean() {
  make -C "${PROJECT_DIR}/Debug" clean
}

run_quality() {
  run_build
  pytest -q "${PROJECT_DIR}/tools/quality/tests" "${PROJECT_DIR}/tools/topology/tests" "${PROJECT_DIR}/tools/topology_designer/tests"
}

case "${1:-build}" in
  build)
    run_build
    ;;
  clean)
    run_clean
    ;;
  quality)
    run_quality
    ;;
  *)
    exec "$@"
    ;;
esac
