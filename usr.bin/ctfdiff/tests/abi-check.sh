#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: abi-check.sh <ctfdiff> <tmpdir>" >&2
	exit 1
fi

CTFDIFF="$1"
TMPDIR="$2"
BASE="$TMPDIR/kernel-original"
NEW_BAD="$TMPDIR/kernel-cb85c2e2e995"
NEW_GOOD="$TMPDIR/kernel-112c453ba910"

if [ ! -x "$CTFDIFF" ]; then
	echo "ctfdiff not found: $CTFDIFF" >&2
	exit 1
fi

for f in "$BASE" "$NEW_BAD" "$NEW_GOOD"; do
	if [ ! -f "$f" ]; then
		echo "missing test input: $f" >&2
		exit 1
	fi
done

rc=0
out="$($CTFDIFF abi-check --base "$BASE" --new "$NEW_BAD" \
	--type "struct bio" 2>&1)" || rc=$?
if [ "$rc" -ne 1 ]; then
	echo "expected exit 1 for NEW_BAD, got $rc" >&2
	echo "$out" >&2
	exit 1
fi

echo "$out" | grep -q "INCOMPATIBLE" || {
	echo "expected INCOMPATIBLE" >&2
	echo "$out" >&2
	exit 1
}

echo "$out" | grep -q "bio_error" || {
	echo "expected bio_error in output" >&2
	echo "$out" >&2
	exit 1
}

rc=0
out="$($CTFDIFF abi-check --base "$BASE" --new "$NEW_GOOD" \
	--type "struct bio" 2>&1)" || rc=$?
if [ "$rc" -ne 0 ]; then
	echo "expected exit 0 for NEW_GOOD, got $rc" >&2
	echo "$out" >&2
	exit 1
fi

echo "$out" | grep -q "COMPATIBLE" || {
	echo "expected COMPATIBLE" >&2
	echo "$out" >&2
	exit 1
}

echo "$out" | grep -q "extension OK: all BASE slots preserved" || {
	echo "expected extension note" >&2
	echo "$out" >&2
	exit 1
}
