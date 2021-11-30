#!/usr/bin/env bash

echo "Flashing microcontroller..."
if [ $# -ne 0 ]
then
    devices="$@"
else
    devices="/dev/tty.SLAB_USBtoUART"
fi
for dev in $devices
do
    if [ -c $dev ]
    then
        esptool.py \
            --chip esp32 \
            --port $dev \
            --baud 921600 \
            --before default_reset \
            --after hard_reset \
            write_flash \
            -z \
            --flash_mode dio \
            --flash_freq 40m \
            --flash_size detect \
            0x1000 build/bootloader/bootloader.bin \
            0x8000 build/partitions_singleapp.bin \
            0x10000 build/lizard.bin \
            || exit 1
    fi
done
