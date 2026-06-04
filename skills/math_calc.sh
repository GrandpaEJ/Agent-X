#!/bin/bash
python3 -c "print($ARG_expression)" 2>/dev/null || bc <<< "$ARG_expression"
