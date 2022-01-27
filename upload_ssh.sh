#!/usr/bin/env bash

if [ $# -lt 1 ]
then
    echo "Usage:"
    echo "`basename $0` [-e/--expander] <user@host>"
    exit
fi

if [[ "$1" == "-e" || "$1" == "--expander" ]]
then
    shift
    remote_command="./upload_expander.sh"
else
    remote_command="./upload_ths1.sh"
fi

host=$1

echo "Copying binary files to $host..."
rsync -zarv --prune-empty-dirs --include="*/" --include="*.bin" --exclude="*" build $host:~/lizard/ || exit 1 # https://stackoverflow.com/a/32527277/3419103

echo "Flashing microcontroller..."
ssh -t $host "bash --login -c 'cd ~/lizard && $remote_command'"
