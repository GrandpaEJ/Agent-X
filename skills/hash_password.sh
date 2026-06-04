#!/bin/bash
openssl passwd -6 "$ARG_password" || python3 -c 'import crypt, sys; print(crypt.crypt(sys.argv[1], crypt.mksalt(crypt.METHOD_SHA512)))' "$ARG_password"
