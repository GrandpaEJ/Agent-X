#!/bin/bash
ping -c 1 8.8.8.8 >/dev/null 2>&1 && echo 'Connected' || echo 'Disconnected'
