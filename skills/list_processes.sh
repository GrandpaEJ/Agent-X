#!/bin/bash
ps -eo pid,ppid,cmd,%mem,%cpu --sort=-%cpu | head -n 15
