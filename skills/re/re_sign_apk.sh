#!/bin/bash
if [ ! -f "$ARG_apk_path" ]; then
    echo "{\"error\": \"Unsigned APK file not found\"}"
    exit 1
fi

mkdir -p "$(dirname "$ARG_out_apk")"

if command -v apksigner >/dev/null 2>&1; then
    apksigner sign --out "$ARG_out_apk" "$ARG_apk_path"
    if [ $? -eq 0 ]; then
        echo "APK signed successfully with apksigner: $ARG_out_apk"
        exit 0
    fi
fi

if command -v jarsigner >/dev/null 2>&1; then
    KEYSTORE="tmp_test.keystore"
    if [ ! -f "$KEYSTORE" ]; then
        keytool -genkey -v -keystore "$KEYSTORE" -alias testkey -keyalg RSA -keysize 2048 -validity 10000 \
          -storepass password -keypass password -dname "CN=Test, O=Test, C=US" >/dev/null 2>&1
    fi
    cp "$ARG_apk_path" "$ARG_out_apk"
    jarsigner -keystore "$KEYSTORE" -storepass password -keypass password "$ARG_out_apk" testkey >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "APK signed successfully with jarsigner: $ARG_out_apk"
        exit 0
    fi
fi

echo "{\"error\": \"Signing failed. Neither 'apksigner' nor 'jarsigner' was found in path. Please install Java Development Kit (JDK) or Android build-tools.\"}"
exit 1
