#!/usr/bin/env bash

echo "Configuring pins..."
echo 'g13 = Output()' > /dev/ttyTHS1
echo 'g14 = Output()' > /dev/ttyTHS1

echo "Bringing microcontroller into flash mode..."
echo 'g13.off()' > /dev/ttyTHS1
echo 'g14.off()' > /dev/ttyTHS1
echo 'g14.on()' > /dev/ttyTHS1
echo 'g13.on()' > /dev/ttyTHS1

echo "Flashing microcontroller..."
dev=/dev/serial/by-id/usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_f276e472a1e3ea118d06177c994a5d01-if00-port0
~/.local/bin/esptool.py \
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
    0x10000 build/lizard.bin

echo "Bringing microcontroller back into normal operation mode..."
echo 'g14.off()' > /dev/ttyTHS1
echo 'g14.on()' > /dev/ttyTHS1
