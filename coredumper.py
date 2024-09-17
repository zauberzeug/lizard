from esp_coredump import CoreDump
import subprocess

# Configuration
serial_port = "/dev/ttyUSB0"  # Update this to your correct serial port (e.g., COM3 on Windows)
chip_type = "esp32"
elf_file = "build/lizard.elf"  # Path to your ELF file from your build
core_dump_file = "core.elf"     # File where the core dump will be saved

def retrieve_core_dump():
    """Retrieve the core dump from ESP32's flash and save it as an ELF file."""
    try:
        print("Retrieving core dump from flash...")
        # Use espcoredump.py to retrieve the core dump in ELF format
        result = subprocess.run([
            "python", 
            f"{subprocess.os.environ.get('IDF_PATH')}/components/espcoredump/espcoredump.py", 
            "info_corefile", "flash", 
            "--port", serial_port, 
            "--chip", chip_type, 
            "--core-format", "elf", 
            "--save-core", core_dump_file
        ], check=True)
        print("Core dump retrieved and saved as:", core_dump_file)
    except subprocess.CalledProcessError as e:
        print("Failed to retrieve core dump:", e)
        exit(1)

def analyze_core_dump():
    """Analyze the retrieved core dump using CoreDump class."""
    print("Analyzing core dump...")
    coredump = CoreDump(chip=chip_type, core=core_dump_file, core_format='elf', prog=elf_file)
    
    # Print core dump info
    coredump.info_corefile()
    
    # Start GDB debugging session
    coredump.dbg_corefile()

if __name__ == "__main__":
    # Step 1: Retrieve core dump from the ESP32 flash
    retrieve_core_dump()
    
    # Step 2: Analyze the retrieved core dump
    analyze_core_dump()


    ## espcoredump.py info_corefile flash
    # with 5.3.1 idf.py coreinfo if menuconfig is set 