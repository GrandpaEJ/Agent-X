#!/bin/bash
termux-vibrate -d "$ARG_duration_ms" || echo 'Termux API not active'
