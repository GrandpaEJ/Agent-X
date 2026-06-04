#!/bin/bash
python3 -c 'import urllib.parse, sys; print(urllib.parse.unquote(sys.stdin.read().strip()))' <<< "$ARG_text"
