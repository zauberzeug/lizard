#!/usr/bin/env bash

if [ $# -lt 1 ]
then
    echo "Usage:"
    echo "`basename $0` [-e/--expander] user@host"
    exit
fi

if [[ "$1" == "-e" || "$1" == "--expander" ]]
then
    shift
    dev="/dev/serial/by-id/usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_f276e472a1e3ea118d06177c994a5d01-if00-port0"
else
    dev="/dev/ttyTHS1"
fi

host=$1

ssh -t $host "bash --login -c 'cd ~/lizard && ./monitor.py $dev'"
