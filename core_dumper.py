import os
import subprocess
import argparse

def read_core_dump(port, baud):
    """Reads core dump information from ESP32 flash using idf.py."""
    try:
        command = [
            'idf.py', 'coredump-info', '-p', port, '-b', baud
        ]
        subprocess.run(command, check=True)
        print("Core dump information retrieved.")
    except subprocess.CalledProcessError as e:
        print(f"Error reading core dump: {e}")

def analyze_core_dump(port, baud, app_elf):
    """Analyzes the core dump using idf.py coredump-debug."""
    try:
        print("Starting GDB session. Type 'exit' to quit the session.")
        command = [
            'idf.py', 'coredump-debug', '-p', port, '-b', baud
        ]
        subprocess.run(command, check=True)
    except subprocess.CalledProcessError as e:
        print(f"Error analyzing core dump: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description=(
            'ESP32 Core Dump Utility using idf.py. '
            'If no options are provided, only the core dump info will be shown. '
            'Use --debug to start a debugging session.'
        )
    )

    # Optional arguments for serial port, baud rate, and ELF file
    parser.add_argument('-p', '--port', default="/dev/ttyUSB0", help='Serial port (default: /dev/ttyUSB0)')
    parser.add_argument('-b', '--baud', default="115200", help='Baud rate (default: 115200)')
    parser.add_argument('-elf', '--elf_file', default="build/lizard.elf", help='ELF file (default: build/lizard.elf)')
    
    # Only provide debug action now
    parser.add_argument('--debug', action='store_true', help='Start a debug session')

    args = parser.parse_args()

    # Step 1: Perform Core Dump Info by default
    if not args.debug:
        read_core_dump(args.port, args.baud)

    # Step 2: Start Debugging Session if --debug is provided
    if args.debug:
        analyze_core_dump(args.port, args.baud, args.elf_file)
