#!/bin/bash
systemctl restart "$ARG_service" && echo 'Service restarted'
