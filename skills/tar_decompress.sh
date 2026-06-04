#!/bin/bash
mkdir -p "$ARG_dest" && tar -xzf "$ARG_archive" -C "$ARG_dest" && echo 'Extracted successfully'
