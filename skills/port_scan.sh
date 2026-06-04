#!/bin/bash
nc -z -v -w5 "$ARG_host" $(echo "$ARG_port_range" | tr '-' ' ') 2>&1
