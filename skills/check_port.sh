#!/bin/bash
lsof -i :"$ARG_port" || ss -lptn 'sport = :'$ARG_port || netstat -tulpn | grep :"$ARG_port"
