#!/usr/bin/env bash

if [ $# -ne 1 ]
then
    echo "Usage:"
    echo "`basename $0` user@host"
    exit
fi

host=$1

echo "Copying binary files to $host..."
rsync -zarv --prune-empty-dirs --include="*/" --include="*.bin" --exclude="*" build $host:~/lizard/ || exit 1 # https://stackoverflow.com/a/32527277/3419103

echo "Flashing microcontroller..."
ssh -t $host 'cd ~/lizard && ./upload_ths1.sh'
