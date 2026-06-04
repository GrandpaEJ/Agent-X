#!/bin/bash
systemctl status "$ARG_service" || rc-service "$ARG_service" status
