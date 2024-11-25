#!/usr/bin/env bash

if [ $# -eq 0 ]
then
    echo "Usage:"
    echo "`basename $0` <addresses>"
    exit
fi

addr2line=~/esp/esp-tools_4.2/tools/xtensa-esp32-elf/esp-2020r3-8.4.0/xtensa-esp32-elf/bin/xtensa-esp32-elf-addr2line
addresses=$@
$addr2line -e build/lizard.elf $addresses
