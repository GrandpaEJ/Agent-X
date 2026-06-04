#!/bin/bash
upower -i $(upower -e | grep 'BAT') 2>/dev/null | grep -E 'state|to full|percentage' || acpi 2>/dev/null || echo 'Battery info not available'
