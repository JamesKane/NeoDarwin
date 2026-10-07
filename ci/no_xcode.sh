#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# P0-02's exit check: build and run //toolchains:hello_cc and :hello_swift
# (plus //tests/smoke) with the pinned toolchain and Xcode hidden, then show
# that the hiding works: the same build without the pinned SDK, or on the
# Xcode toolchain (xcrun), fails.
#
# Hiding Xcode, without touching the installed one or xcode-select:
#   - Bazel runs from `env -i` with DEVELOPER_DIR set to an empty directory,
#     so /usr/bin/xcrun and every /usr/bin developer-tool shim (clang, ld,
#     swiftc, ...) fail, in repository rules and in actions alike
#     (--repo_env, --action_env, --host_action_env), and PATH holds only
#     /usr/bin:/bin:/usr/sbin:/sbin;
#   - a separate output base, so every repository (the toolchains, the SDK
#     check, apple_support's Xcode detection) is fetched fresh in that
#     environment. Downloads come from the shared repository cache.
#
#   ci/no_xcode.sh            positive build and tests, then the two negative checks
# Env: ND_NO_XCODE_OUTPUT_BASE (default ~/Library/Caches/bazel/neodarwin-no-xcode).
set -euo pipefail
cd "$(dirname "$0")/.."
bazel="$(command -v bazel)"
base="${ND_NO_XCODE_OUTPUT_BASE:-$HOME/Library/Caches/bazel/neodarwin-no-xcode}"
empty="$(mktemp -d "${TMPDIR:-/tmp}/nd-no-xcode.XXXXXX")"
trap 'rm -rf "$empty"' EXIT

hidden() {
	env -i HOME="$HOME" USER="${USER:-}" TMPDIR="${TMPDIR:-/tmp}" PATH=/usr/bin:/bin:/usr/sbin:/sbin \
		DEVELOPER_DIR="$empty" "$bazel" --output_base="$base" "$@"
}
hide_flags=(
	--repo_env=DEVELOPER_DIR="$empty"
	--action_env=DEVELOPER_DIR="$empty"
	--host_action_env=DEVELOPER_DIR="$empty"
)
targets=(//toolchains:hello_cc //toolchains:hello_swift //tests/smoke:smoke)
tests=(//toolchains:hello_cc_test //toolchains:hello_swift_test //tests/smoke:smoke_test)

# 0. The environment really hides Xcode.
if DEVELOPER_DIR="$empty" /usr/bin/xcrun --find clang >/dev/null 2>&1; then
	echo "no_xcode: xcrun still works with DEVELOPER_DIR=$empty" >&2; exit 1
fi

# 1. Positive: the pinned toolchain builds and runs the hello worlds.
echo "no_xcode: building ${targets[*]} with --config=pinned, Xcode hidden"
hidden test --config=pinned "${hide_flags[@]}" "${targets[@]}" "${tests[@]}"
bin="$(hidden info --config=pinned "${hide_flags[@]}" bazel-bin 2>/dev/null)"
"$bin/toolchains/hello_cc"
"$bin/toolchains/hello_swift"

# 2. Negative: an SDK that isn't the pinned one fails at fetch, naming it.
fake="$empty/MacOSX27.0.sdk"
mkdir -p "$fake"
printf '{"Version":"27.0"}\n' > "$fake/SDKSettings.json"
log="$empty/sdk.log"
if hidden build --config=pinned "${hide_flags[@]}" --repo_env=ND_MACOS_SDK="$fake" //toolchains:hello_cc >"$log" 2>&1; then
	echo "no_xcode: built with a fake SDK; the SDK pin isn't checked" >&2; exit 1
fi
if ! grep -q "nd_macos_sdk: no macOS 27.0 SDK with content sha256" "$log"; then
	cat "$log" >&2; echo "no_xcode: the fake-SDK build failed, but not at the SDK check" >&2; exit 1
fi
echo "no_xcode: a fake SDK fails at fetch, as it should:"
grep -m1 -A1 "Error in fail: nd_macos_sdk" "$log" | sed 's/^/    /'

# 3. Negative: the Xcode toolchain (xcrun) doesn't work in this environment,
# so step 1 can't have used it.
log="$empty/xcode.log"
if hidden build "${hide_flags[@]}" //toolchains:hello_cc >"$log" 2>&1; then
	echo "no_xcode: the default (Xcode) toolchain still builds with Xcode hidden" >&2; exit 1
fi
echo "no_xcode: without --config=pinned the build fails, as it should:"
grep -m3 -i "error" "$log" | sed 's/^/    /'

# Leave the output base's SDK repository verified against the real SDK.
hidden fetch --config=pinned "${hide_flags[@]}" --repo=@nd_macos_sdk >/dev/null 2>&1 || true
echo "no_xcode: OK"
