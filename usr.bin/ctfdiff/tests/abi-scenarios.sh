#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: abi-scenarios.sh <ctfdiff>" >&2
	exit 1
fi

CTFDIFF="$1"
if [ ! -x "$CTFDIFF" ]; then
	echo "ctfdiff not found: $CTFDIFF" >&2
	exit 1
fi

: "${CC:=cc}"
: "${CXX:=c++}"
: "${CTFCONVERT:=ctfconvert}"
: "${CTFCONVERT_FLAGS:=-l ctfdiff-test -i}"

if ! command -v "$CTFCONVERT" >/dev/null 2>&1; then
	echo "SKIP: ctfconvert not found" >&2
	exit 0
fi

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)
WORKDIR=$(mktemp -d -t ctfdiff.XXXXXX)
trap 'rm -rf "$WORKDIR"' EXIT

BASE_SRC="$SCRIPT_DIR/abi_base.c"
NEW_SRC="$SCRIPT_DIR/abi_new.c"
CPP_SRC="$SCRIPT_DIR/abi_dummy.cc"
TYPES_FILE="$SCRIPT_DIR/abi_types.txt"

"$CC" -g -O0 -c "$BASE_SRC" -o "$WORKDIR/base.o"
"$CC" -g -O0 -c "$NEW_SRC" -o "$WORKDIR/new.o"
"$CXX" -g -O0 -c "$CPP_SRC" -o "$WORKDIR/dummy.o"

"$CTFCONVERT" $CTFCONVERT_FLAGS -o "$WORKDIR/base.ctf" "$WORKDIR/base.o"
"$CTFCONVERT" $CTFCONVERT_FLAGS -o "$WORKDIR/new.ctf" "$WORKDIR/new.o"

if [ ! -f "$WORKDIR/base.ctf" ] || [ ! -f "$WORKDIR/new.ctf" ]; then
	echo "SKIP: ctfconvert did not produce CTF output" >&2
	exit 0
fi

run_case()
{
	desc="$1"
	expected_rc="$2"
	shift 2

	rc=0
	out="$($CTFDIFF abi-check "$@" 2>&1)" || rc=$?
	if [ "$rc" -ne "$expected_rc" ]; then
		echo "FAIL: $desc (expected rc=$expected_rc got rc=$rc)" >&2
		echo "$out" >&2
		exit 1
	fi

	printf '%s\n' "$out"
}

expect_grep()
{
	desc="$1"
	pattern="$2"
	if ! printf '%s\n' "$3" | grep -q "$pattern"; then
		echo "FAIL: $desc (missing pattern: $pattern)" >&2
		echo "$3" >&2
		exit 1
	fi
}

out=$(run_case "tail_ok compatible" 0 --base "$WORKDIR/base.ctf" \
	--new "$WORKDIR/new.ctf" --type "struct tail_ok")
expect_grep "tail_ok compatible" "COMPATIBLE" "$out"
expect_grep "tail_ok extension" "extension OK: all BASE slots preserved" "$out"

out=$(run_case "tail_bad incompatible" 1 --base "$WORKDIR/base.ctf" \
	--new "$WORKDIR/new.ctf" --type "struct tail_bad")
expect_grep "tail_bad incompatible" "INCOMPATIBLE" "$out"

out=$(run_case "rename_ok compatible" 0 --base "$WORKDIR/base.ctf" \
	--new "$WORKDIR/new.ctf" --type "struct rename_ok")
expect_grep "rename_ok compatible" "COMPATIBLE" "$out"

out=$(run_case "ptr_ok compatible" 0 --base "$WORKDIR/base.ctf" \
	--new "$WORKDIR/new.ctf" --type "struct ptr_ok")
expect_grep "ptr_ok compatible" "COMPATIBLE" "$out"

out=$(run_case "array_bad incompatible" 1 --base "$WORKDIR/base.ctf" \
	--new "$WORKDIR/new.ctf" --type "struct array_bad")
expect_grep "array_bad incompatible" "INCOMPATIBLE" "$out"

out=$(run_case "bitfield_bad incompatible" 1 --base "$WORKDIR/base.ctf" \
	--new "$WORKDIR/new.ctf" --type "struct bitfield_bad")
expect_grep "bitfield_bad width" "WIDTH_CHANGED" "$out"

out=$(run_case "embed_bad incompatible" 1 --base "$WORKDIR/base.ctf" \
	--new "$WORKDIR/new.ctf" --type "struct embed_bad")
expect_grep "embed_bad" "EMBEDDED_LAYOUT_CHANGED" "$out"

out=$(run_case "signed_bad incompatible" 1 --base "$WORKDIR/base.ctf" \
	--new "$WORKDIR/new.ctf" --type "struct signed_bad")
expect_grep "signed_bad" "TYPE_CHANGED" "$out"

out=$(run_case "enum_ok compatible" 0 --base "$WORKDIR/base.ctf" \
	--new "$WORKDIR/new.ctf" --type "enum enum_ok")
expect_grep "enum_ok" "COMPATIBLE" "$out"

out=$(run_case "unknown type" 3 --base "$WORKDIR/base.ctf" \
	--new "$WORKDIR/new.ctf" --type "struct missing_type")
expect_grep "unknown type" "UNKNOWN" "$out"

out=$(run_case "directional incompatible" 1 --base "$WORKDIR/new.ctf" \
	--new "$WORKDIR/base.ctf" --type "struct tail_ok")
expect_grep "directional" "INCOMPATIBLE" "$out"

out=$(run_case "types-file mixed" 1 --base "$WORKDIR/base.ctf" \
	--new "$WORKDIR/new.ctf" --types-file "$TYPES_FILE")
expect_grep "types-file" "tail_ok" "$out"
expect_grep "types-file" "tail_bad" "$out"

out=$(run_case "json output" 1 --json --base "$WORKDIR/base.ctf" \
	--new "$WORKDIR/new.ctf" --type "struct tail_bad")
expect_grep "json tool_version" "tool_version" "$out"
expect_grep "json verdict" "\"verdict\":\"INCOMPATIBLE\"" "$out"
expect_grep "json type" "\"type\":\"struct tail_bad\"" "$out"
