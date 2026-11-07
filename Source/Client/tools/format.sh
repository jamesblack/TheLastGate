#!/usr/bin/env bash
#
# Formats the client C/C++ lines that changed since a base branch, using the
# .clang-format next to this directory. Untouched lines (and untouched files)
# are left alone, so legacy code is only reformatted where it is edited.
#
# Usage: tools/format.sh [--check] [base]
#   --check  report unformatted lines and exit 1 instead of rewriting files
#   base     branch or commit to compare against (default: master)
#
# clang-format is taken from $CLANG_FORMAT, then PATH, then CLion's bundled copy.

set -euo pipefail

cd "$(dirname "$0")/.."

check_only=0
if [ "${1:-}" = "--check" ]; then
    check_only=1
    shift
fi
base="${1:-master}"

find_clang_format() {
    if [ -n "${CLANG_FORMAT:-}" ]; then
        echo "$CLANG_FORMAT"
        return
    fi
    if command -v clang-format >/dev/null 2>&1; then
        command -v clang-format
        return
    fi
    local clion_copy
    for clion_copy in \
        "$LOCALAPPDATA/Programs/CLion/plugins/clion-radler/DotFiles/windows-x64/clang-format.exe" \
        "$LOCALAPPDATA/Programs/CLion/bin/clang/win/x64/bin/clang-format.exe"; do
        if [ -x "$clion_copy" ]; then
            echo "$clion_copy"
            return
        fi
    done
    echo "clang-format not found; set CLANG_FORMAT to its path" >&2
    exit 2
}

clang_format="$(find_clang_format)"

is_formattable() {
    case "$1" in
        external/* | src/ui/imgui/* | cmake-build*/*) return 1 ;;
        *.c | *.cpp | *.h | *.hpp) ;;
        *) return 1 ;;
    esac
    ! is_tab_indented "$1"
}

# Legacy files indented with tabs keep their own layout; formatting a few
# edited lines would leave them half tabs, half spaces.
is_tab_indented() {
    local tab_lines space_lines
    tab_lines=$(grep -c $'^\t' "$1" || true)
    space_lines=$(grep -c '^    ' "$1" || true)
    [ "$tab_lines" -gt "$space_lines" ]
}

# Prints "--lines=start:end" for each block of lines that differs from the base.
changed_line_ranges() {
    git diff -U0 --no-color "$base" -- "$1" |
        sed -n 's/^@@ -[0-9,]* +\([0-9]*\)\(,\([0-9]*\)\)\{0,1\} @@.*/\1 \3/p' |
        while read -r start count; do
            count="${count:-1}"
            if [ "$count" -gt 0 ]; then
                echo "--lines=$start:$((start + count - 1))"
            fi
        done
}

status=0

format_file() {
    local file="$1"
    shift
    if [ "$check_only" -eq 1 ]; then
        if ! "$clang_format" --dry-run --Werror "$@" "$file" 2>/dev/null; then
            echo "needs formatting: $file"
            status=1
        fi
    else
        "$clang_format" -i "$@" "$file"
        echo "formatted: $file"
    fi
}

# Tracked files changed since the base: only their changed lines.
while IFS= read -r file; do
    is_formattable "$file" || continue
    mapfile -t ranges < <(changed_line_ranges "$file")
    [ "${#ranges[@]}" -gt 0 ] || continue
    format_file "$file" "${ranges[@]}"
done < <(git diff --name-only --diff-filter=ACMR --relative "$base" -- .)

# New files not yet committed: the whole file.
while IFS= read -r file; do
    is_formattable "$file" || continue
    format_file "$file"
done < <(git ls-files --others --exclude-standard -- .)

exit "$status"
