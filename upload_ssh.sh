#!/usr/bin/env bash

if [ $# -ne 1 ]
then
    echo "Usage:"
    echo "`basename $0` user@host"
    exit
fi

host=$1

echo "Copying binary files to $host..."
scp build/bootloader/bootloader.bin \
    build/partitions_singleapp.bin \
    build/lizard.bin \
    $host:~/lizard/ || exit 1

echo "Flashing microcontroller..."
ssh -t $host 'cd ~/lizard && ./upload_ths1.sh'
