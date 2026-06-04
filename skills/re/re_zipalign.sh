#!/bin/bash
if [ ! -f "$ARG_apk_path" ]; then
    echo "{\"error\": \"APK file not found\"}"
    exit 1
fi

mkdir -p "$(dirname "$ARG_out_apk")"

if command -v zipalign >/dev/null 2>&1; then
    zipalign -f -p 4 "$ARG_apk_path" "$ARG_out_apk"
    if [ $? -eq 0 ]; then
        echo "APK aligned successfully: $ARG_out_apk"
        exit 0
    else
        echo "{\"error\": \"zipalign command execution failed\"}"
        exit 1
    fi
else
    echo "{\"error\": \"zipalign utility not found in PATH. Please install Android build-tools.\"}"
    exit 1
fi
