#!/bin/bash
sed -i "s|$ARG_find|$ARG_replace|g" "$ARG_path" && echo 'Replacements complete'
