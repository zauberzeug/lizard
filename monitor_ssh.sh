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
#
# The remote runs its own checkout, so it has to be new enough to auto-detect: a monitor.py
# from before serial_devices.py still walks a hard-coded path list and takes /dev/ttyTHS0 on
# an L4T 36 brain, where the microcontroller is on /dev/ttyTHS1. Pull on the target first, or
# pass the path by hand there.
ssh -t $host "bash --login -c 'cd ~/lizard && ./monitor.py'"
