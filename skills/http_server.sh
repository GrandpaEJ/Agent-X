#!/bin/bash
echo "Spawning HTTP server on port $ARG_port... Ctrl+C in system process list to stop."
python3 -m http.server "$ARG_port" &
