#!/bin/bash
python3 -c 'import urllib.parse, sys; print(urllib.parse.quote(sys.stdin.read().strip()))' <<< "$ARG_text"
