#!/usr/bin/env bash

if [ $# -ne 1 ]
then
    echo "Usage:"
    echo "`basename $0` user@host"
    exit
fi

host=$1

ssh -t $host "bash --login -c 'cd ~/lizard && ./monitor.py'"
