#!/bin/bash
date -d "$ARG_date_str" +%s 2>/dev/null || python3 -c 'import dateutil.parser, sys; print(int(dateutil.parser.parse(sys.argv[1]).timestamp()))' "$ARG_date_str"
