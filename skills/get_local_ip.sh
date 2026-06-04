#!/bin/bash
ip route get 1.1.1.1 2>/dev/null | awk '{print $7}' || hostname -I
