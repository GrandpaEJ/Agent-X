#!/bin/bash
curl -s "https://wttr.in/${ARG_city}?format=3" || curl -s "https://wttr.in/${ARG_city}?n=1"
