#!/bin/bash
sensors 2>/dev/null || cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | awk '{print $1/1000 " C"}' || echo 'Sensors not available'
