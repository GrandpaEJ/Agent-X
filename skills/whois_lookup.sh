#!/bin/bash
whois "$ARG_domain" 2>/dev/null || echo 'Whois not installed or failed'
