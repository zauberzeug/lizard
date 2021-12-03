#!/usr/bin/env bash

echo "Configuring pins..."
en=216
g0=50
sudo echo $en > /sys/class/gpio/export
sudo echo $g0 > /sys/class/gpio/export
sudo echo out > /sys/class/gpio/gpio$en/direction
sudo echo out > /sys/class/gpio/gpio$g0/direction

echo "Bringing microcontroller into flash mode..."
sudo echo 1 > /sys/class/gpio/gpio$en/value
sudo echo 1 > /sys/class/gpio/gpio$g0/value
sudo echo 0 > /sys/class/gpio/gpio$en/value

echo "Flashing microcontroller..."
esptool.py \
    --chip esp32 \
    --port /dev/ttyTHS1 \
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
    0x10000 build/lizard.bin

echo "Bringing microcontroller back into normal operation mode..."
sudo echo 0 > /sys/class/gpio/gpio$g0/value
sudo echo 1 > /sys/class/gpio/gpio$en/value
sudo echo 0 > /sys/class/gpio/gpio$en/value

echo "Release pin configuration..."
sudo echo $en > /sys/class/gpio/unexport
sudo echo $g0 > /sys/class/gpio/unexport
