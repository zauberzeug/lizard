#!/usr/bin/env bash

echo "1/3 Generating parser..."
if [[ "language.owl" -nt src/parser.h ]]
then
    owl/owl -c language.owl -o src/parser.h
    if [[ $? -ne 0 ]]
    then
        rm -f src/parser.h 
        exit 1
    fi
else
    echo "Nothing to do."
fi

echo "2/3 Compiling Lizard..."
docker run --rm -v $PWD:/project -w /project espressif/idf:v4.2 make -j4 || exit 1

echo "3/3 Flash microcontroller..."
if [ $# -ne 0 ]
then
    devices="$@"
else
    devices="/dev/tty.SLAB_USBtoUART /dev/ttyTHS1 /dev/ttyUSB0"
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
