#!/bin/bash
if [ ! -f "$ARG_file_path" ]; then
    echo "{\"error\": \"Binary file not found\"}"
    exit 1
fi
MIN_LEN="${ARG_min_len:-4}"
if [ -n "$ARG_pattern" ]; then
    strings -n "$MIN_LEN" "$ARG_file_path" | grep -Ei "$ARG_pattern" | head -n 100
else
    strings -n "$MIN_LEN" "$ARG_file_path" | head -n 100
fi
