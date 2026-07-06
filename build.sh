#!/bin/bash
set -euo pipefail

LOG_FILE="build.log"
exec > >(tee "$LOG_FILE") 2>&1

echo "=== build.sh started: $(date) ==="
echo "=== pwd: $(pwd) ==="
echo "=== docker: $(docker --version 2>&1) ==="
echo ""

run_step() {
    local label="$1"; shift
    echo "--- STEP: $label ---"
    if "$@"; then
        echo "--- OK: $label ---"
    else
        local rc=$?
        echo "--- FAILED: $label (exit $rc) ---"
        exit $rc
    fi
    echo ""
}

run_step "apply vendored libdragon patches (fgeom/rspq_profile backports for tiny3d)" \
    cp -r patches/libdragon/. libdragon/

run_step "docker image pull check" \
    docker image inspect ghcr.io/dragonminded/libdragon:latest --format '{{.Id}}'

run_step "libdragon install + tiny3d build + make" \
    docker run --rm \
        -v "$(pwd)":/project \
        -w /project \
        ghcr.io/dragonminded/libdragon:latest \
        sh -c '
            set -e
            echo "=== [container] PATH: $PATH ==="
            echo "=== [container] N64_INST: ${N64_INST:-unset} ==="
            echo ""

            echo "--- [container] STEP: libdragon install ---"
            make -C libdragon install tools-install
            echo "--- [container] OK: libdragon install ---"
            echo ""

            echo "--- [container] STEP: tiny3d build ---"
            make -C tiny3d CFLAGS="-Wno-error" CXXFLAGS="-Wno-error"
            echo "--- [container] OK: tiny3d build ---"
            echo ""

            echo "--- [container] STEP: project make ---"
            make "$@"
            echo "--- [container] OK: project make ---"
        ' -- "$@"

echo ""
echo "=== build.sh finished OK: $(date) ==="
echo "=== ROM: $(ls -lh crux64.z64 2>/dev/null || echo 'not found') ==="
