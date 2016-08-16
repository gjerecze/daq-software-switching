#!/bin/sh
ps -C daqswitch -o tid H h | tail -n +2 | awk -vORS=, '{ print $1 }'
