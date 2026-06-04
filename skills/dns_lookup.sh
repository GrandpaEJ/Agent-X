#!/bin/bash
host "$ARG_domain" || nslookup "$ARG_domain"
