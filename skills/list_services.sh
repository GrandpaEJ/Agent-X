#!/bin/bash
systemctl list-units --type=service --state=running || rc-status 2>/dev/null || echo 'systemd/openrc not available'
