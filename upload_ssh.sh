#!/usr/bin/env bash

if [ $# -lt 1 ]
then
    echo "Usage:"
    echo "`basename $0` <user@host> [flash args]"
    exit
fi

host=$1

echo "Copying binary files to $host..."
rsync -zarv --prune-empty-dirs --include="*/" --include="*.bin" --exclude="*" build $host:~/lizard/ || exit 1 # https://stackoverflow.com/a/32527277/3419103

echo "Flashing microcontroller..."
ssh -t $host "bash --login -c 'cd ~/lizard && sudo ./flash.py ${@:2}'"
