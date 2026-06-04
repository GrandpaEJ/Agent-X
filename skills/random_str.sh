#!/bin/bash
tr -dc 'A-Za-z0-9!@#%^&*' </dev/urandom | head -c "$ARG_length" && echo
