#!/bin/bash
if [[ "$ARG_pid_or_name" =~ ^[0-9]+$ ]]; then kill -9 "$ARG_pid_or_name" && echo 'Killed PID'; else pkill -f "$ARG_pid_or_name" && echo 'Killed matched name'; fi
