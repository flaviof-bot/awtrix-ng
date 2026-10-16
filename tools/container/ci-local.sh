#!/usr/bin/env bash
# Run from the repository root, inside the local CI image.
set -euo pipefail

usage() {
    printf 'Usage: %s {native|linux|webui|docs|mkdocs|build ENV...|all}...\n' "$0" >&2
}
if [[ ! -f platformio.ini || ! -f requirements-docs.txt ]]; then
    printf 'Run this script from the repository root.\n' >&2
    exit 2
fi
if (( $# == 0 )); then usage; exit 2; fi

# Validate the entire request before doing any work. A build consumes environment
# names until the next subcommand, so groups can be freely combined.
steps=()
while (( $# )); do
    case "$1" in
        native|linux|webui|docs|mkdocs) steps+=("$1"); shift ;;
        all)
            # M3a adds galactic_unicorn and galactic_unicorn_2w here.
            steps+=(native linux webui docs mkdocs build:awtrix build:awtrix_s3_octal build:awtrix_s3_quad)
            shift ;;
        build)
            shift
            count=0
            while (( $# )); do
                case "$1" in native|linux|webui|docs|mkdocs|build|all) break ;; esac
                if [[ ! "$1" =~ ^[A-Za-z0-9_][A-Za-z0-9_.-]*$ ]] || ! grep -qFx "[env:$1]" platformio.ini; then
                    printf 'Invalid environment: %s\n' "$1" >&2; exit 2
                fi
                steps+=("build:$1"); count=$((count + 1)); shift
            done
            if (( count == 0 )); then usage; exit 2; fi ;;
        *) printf 'Unknown subcommand: %s\n' "$1" >&2; usage; exit 2 ;;
    esac
done

failed=0
run_step() {
    local label=$1 start=$SECONDS rc=0
    shift
    printf '\n=== %s ===\n' "$label"
    if "$@"; then
        printf 'PASS %s (%ds)\n' "$label" "$((SECONDS - start))"
    else
        rc=$?
        printf 'FAIL %s (%ds; exit %d)\n' "$label" "$((SECONDS - start))" "$rc"
        failed=1
    fi
}
webui_tests() (
    cd webui/test && npm ci && npm test
)
# Missing anchors are INFO messages, not errors even with MkDocs strict mode.
mkdocs_build() {
    local log rc=0
    log=$(mktemp)
    python tools/docs/build_site.py 2>&1 | tee "$log" || rc=$?
    if grep -q "does not contain an anchor" "$log"; then
        grep "does not contain an anchor" "$log"
        rc=1
    fi
    rm -f "$log"
    return "$rc"
}
tag_check() {
    local version
    version=$(tr -d '[:space:]' < version)
    [[ "${GITHUB_REF_NAME:-}" == "v$version" ]] &&
        [[ -f "docs/releases/$version.md" ]] &&
        grep -q "releases/$version.md" mkdocs.yml
}

# Linux permission/TLS contracts deliberately reject uid 0, like upstream's
# unprivileged Actions runner. Only CTest output needs write access to the build.
linux_tests() {
    if (( EUID == 0 )); then
        chown -R ci:ci .pio/linux &&
            runuser -u ci -- ctest --test-dir .pio/linux --output-on-failure --no-tests=error
    else
        ctest --test-dir .pio/linux --output-on-failure --no-tests=error
    fi
}

for step in "${steps[@]}"; do
    case "$step" in
        native) run_step native python scripts/test_native.py ;;
        linux)
            run_step linux-libraries pio pkg install -e native
            run_step linux-configure cmake -S . -B .pio/linux -DCMAKE_BUILD_TYPE=Release -DAWTRIX_REQUIRE_INTEGRATION_SERVICES=ON
            run_step linux-build cmake --build .pio/linux --parallel 2
            run_step linux linux_tests
            printf 'SKIP linux-service (needs systemd; covered by GitHub CI)\n' ;;
        webui)
            run_step webui webui_tests
            run_step flowconv node --test "tools/flowconv/*.test.mjs" ;;
        docs)
            run_step docs-dependencies pip install -q -r requirements-docs.txt pyyaml
            for check in check_docs_sync test_check_error_messages check_error_messages \
                         check_flow_converter gen_agent_skill check_berry_api \
                         check_prelude_solidified check_font_sync check_partitions; do
                if [[ "$check" == gen_agent_skill ]]; then
                    run_step "$check" python "tools/$check.py" --check
                else
                    run_step "$check" python "tools/$check.py"
                fi
            done
            run_step tc002-constants python tools/tc002/install/gen_constants.py --check
            if [[ "${GITHUB_REF:-}" == refs/tags/v* ]]; then
                run_step tag-check tag_check
            else
                printf 'SKIP tag-check (not a v-tag event)\n'
            fi ;;
        mkdocs)
            # docs.yml downloads this prerequisite from tc002-image-tools.yml.
            # Build the same pinned assets locally; do not hide broken links to
            # its generated sources, licenses and build manifest.
            run_step docs-image-tools python tools/tc002/browser-image/build.py --work .pio/docs-image-tools --output docs/assets/tc002/image
            run_step mkdocs mkdocs_build ;;
        build:*) run_step "$step" pio run -e "${step#build:}" ;;
    esac
done
exit "$failed"
