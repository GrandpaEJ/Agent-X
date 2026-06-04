#!/bin/bash
date -d @"$ARG_num" 2>/dev/null || date -r "$ARG_num" 2>/dev/null || python3 -c 'import time, sys; print(time.ctime(int(sys.argv[1])))' "$ARG_epoch"
