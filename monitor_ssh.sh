#!/usr/bin/env bash

if [ $# -lt 1 ]
then
    echo "Usage:"
    echo "`basename $0` <user@host>"
    exit
fi

host=$1
script_dir=$(dirname "$0")

# The monitor is run without a device path, so it derives the Jetson's UART from the L4T version
# (35 -> ttyTHS0, 36 -> ttyTHS1) -- but only a copy that knows how does. An older checkout on the
# target would take the first ttyTHS* node that exists instead and, on a Robot Brain where that is
# not the microcontroller's UART, connect to the wrong one without saying so. So send this
# checkout's monitor along, the way espresso.py --host sends espresso.py.
# -p restores the exec bit on a pre-existing non-executable remote copy, whose permissions a plain
# rsync would keep forever ("./monitor.py: Permission denied").
rsync -zp "$script_dir/monitor.py" "$script_dir/serial_devices.py" "$host:lizard/" || exit 1

# -t gives the monitor a terminal: it needs one for its prompt, and for the question it asks when
# several serial devices are attached.
ssh -t $host "bash --login -c 'cd ~/lizard && ./monitor.py'"
