#!/usr/bin/env bash

if [ $# -lt 1 ]
then
    echo "Usage:"
    echo "`basename $0` <user@host>"
    exit
fi

host=$1

# No device path: monitor.py derives the Jetson's UART from the L4T version (35 -> ttyTHS0,
# 36 -> ttyTHS1), so the target's own checkout has to be new enough to do that. -t gives it a
# terminal for the case where several adapters are attached.
ssh -t $host "bash --login -c 'cd ~/lizard && ./monitor.py'"
