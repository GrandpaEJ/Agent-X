#!/bin/bash
xmllint --format "$ARG_path" 2>/dev/null || python3 -c 'import xml.dom.minidom, sys; print(xml.dom.minidom.parse(open(sys.argv[1])).toprettyxml())' "$ARG_path"
