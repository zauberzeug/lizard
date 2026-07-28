#!/usr/bin/env bash

if [ $# -lt 1 ]
then
    echo "Usage:"
    echo "`basename $0` <user@host>"
    exit
fi

host=$1

# no device path: monitor.py derives the Jetson's UART from the L4T version, which differs
# between L4T 35 (/dev/ttyTHS0) and 36 (/dev/ttyTHS1). -t gives it a terminal, so it can ask
# on a development host with several adapters attached.
ssh -t $host "bash --login -c 'cd ~/lizard && ./monitor.py'"
