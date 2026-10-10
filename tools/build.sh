# build.sh -- build the Siyana Lang bootstrap compiler.
#
# Phase 1 exit criteria require that a clean clone builds with one documented
# command. This script is that command.
#
#   ./tools/build.sh          build (debug)
#   ./tools/build.sh release  build (optimised)
#   ./tools/build.sh clean    remove build output
#
# Requires only a C11 compiler and make. No build system generator, no
# external dependencies.

set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="$root/build"
config="${1:-debug}"

case "$config" in
    debug)
        cflags="-std=c11 -O0 -g -Wall -Wextra -Wpedantic"
        ;;
    release)
        cflags="-std=c11 -O2 -DNDEBUG -Wall -Wextra -Wpedantic"
        ;;
    clean)
        rm -rf "$build_dir"
        echo "cleaned"
        exit 0
        ;;
    *)
        echo "usage: $0 [debug|release|clean]" >&2
        exit 1
        ;;
esac

# Honour CC if the environment sets it; otherwise try clang then gcc. This is
# what lets the same script work on the author's machine and on CI runners that
# only provide one of the two.
if [ -z "${CC:-}" ]; then
    if command -v clang >/dev/null 2>&1; then
        CC=clang
    elif command -v gcc >/dev/null 2>&1; then
        CC=gcc
    else
        echo "error: no C compiler found (set CC)" >&2
        exit 1
    fi
fi

mkdir -p "$build_dir"

# mingw32-make names the target 'siyana.exe'; POSIX make on other platforms
# uses 'siyana'. Detect so the same script works in both.
exe="siyana"
case "$(uname -s 2>/dev/null || echo Windows)" in
    MINGW*|MSYS*|CYGWIN*) exe="siyana.exe" ;;
esac

echo "building siyana ($config) with $CC"
echo "  $cflags"

# shellcheck disable=SC2086
"$CC" $cflags -I"$root/bootstrap/include" \
    "$root/bootstrap/main.c" \
    "$root/bootstrap/version.c" \
    -o "$build_dir/$exe"

echo "built: $build_dir/$exe"