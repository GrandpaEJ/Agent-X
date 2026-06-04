#!/bin/bash
if [ ! -f "$ARG_apk_path" ]; then
    echo "{\"error\": \"APK file not found\"}"
    exit 1
fi
mkdir -p "$ARG_out_dir"
unzip -o "$ARG_apk_path" -d "$ARG_out_dir" > /dev/null
if [ $? -eq 0 ]; then
    echo "APK extracted successfully to $ARG_out_dir"
    echo "Contents summary:"
    ls -lh "$ARG_out_dir"
else
    echo "{\"error\": \"Failed to extract APK\"}"
    exit 1
fi
