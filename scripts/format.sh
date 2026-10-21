#!/usr/bin/env bash

FMT=""

for clangfmt in clang-format{,-{4,3}.{9,8,7,6,5,4,3,2,1,0}}; do
    if which "$clangfmt" &>/dev/null; then
        FMT="$clangfmt"
        break
    fi
done

if [ -z "$FMT" ]; then
    echo "failed to find clang-format"
    exit 1
fi

function format_file() {
    file_path=${1}
    echo "format ${file_path}";
    ${FMT} -style=file -i "${file_path}"
}

function format_dir() {
    oldIFS=$IFS
    IFS=$'\n'

    for f in $(find $@ -type d \
                       -name '*.h' -or \
                       -name '*.c'); do
        format_file "${f}";
    done

    IFS=$oldIFS

    echo "~~~ $@ Done ~~~";
}

for x in $@; do
    if [ -f "${x}" ]; then
        format_file "${x}"
    elif [ -d "${x}" ]; then
        format_dir "${x}";
    else
        echo "Invalid path: ${x}"
    fi
done
