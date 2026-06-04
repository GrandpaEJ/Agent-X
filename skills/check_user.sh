#!/bin/bash
id "$ARG_username" 2>/dev/null || grep -q "^$ARG_username:" /etc/passwd && echo 'User exists' || echo 'User does not exist'
