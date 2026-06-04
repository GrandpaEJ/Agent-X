#!/bin/bash
traceroute -m 15 "$ARG_host" || tracepath -m 15 "$ARG_host"
