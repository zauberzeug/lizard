#!/usr/bin/env bash

docker run --rm -v $PWD:/project -w /project espressif/idf:v4.2 make -j4 || exit 1
esptool.py \
    --chip esp32 \
    --port /dev/tty.SLAB_USBtoUART \
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
