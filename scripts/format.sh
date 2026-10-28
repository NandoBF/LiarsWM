#!/usr/bin/env bash

cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1

while [[ ! -d "src" && ! -d "include" ]]; do
    if [[ "$PWD" == "/" ]]; then
        echo "No src/ or include/ directories found in parent directories."
        exit 1
    fi
    cd ..
done

FMT=""

for clangfmt in clang-format{,-{20..3}}; do
    if command -v "$clangfmt" &>/dev/null; then
        FMT="$clangfmt"
        break
    fi
done

if [[ -z "$FMT" ]]; then
    echo "Error: failed to find clang-format"
    exit 1
fi

echo "Using ${FMT}..."

files_to_check=()
files_to_format=()

SEARCH_DIRS=()
[[ -d "src" ]] && SEARCH_DIRS+=("src")
[[ -d "include" ]] && SEARCH_DIRS+=("include")

if [[ ${#SEARCH_DIRS[@]} -gt 0 ]]; then
    while IFS= read -r -d '' file; do
        files_to_check+=("$file")
    done < <(find -L "${SEARCH_DIRS[@]}" -type f \( -name '*.c' -o -name '*.h' \) -print0)
fi

if [[ ${#files_to_check[@]} -eq 0 ]]; then
    echo "No .c or .h files found in src/ or include/."
    exit 0
fi

echo "Scanning ${#files_to_check[@]} files..."

for file in "${files_to_check[@]}"; do
    if "$FMT" -style=file -output-replacements-xml "$file" | grep -q "<replacement "; then
        echo "Needs formatting: $file"
        files_to_format+=("$file")
    fi
done

if [[ ${#files_to_format[@]} -eq 0 ]]; then
    echo "All files are already properly formatted!"
    exit 0
fi

echo ""
read -p "Do you want to format these ${#files_to_format[@]} files? [y/N]: " choice

case "$choice" in 
    y|Y )
        for file in "${files_to_format[@]}"; do
            echo "Formatting $file..."
            "$FMT" -style=file -i "$file"
        done
        echo "~~~ Done ~~~"
        ;;
    * )
        echo "Formatting aborted. No files were changed."
        ;;
esac
