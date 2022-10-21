# Lizard

Lizard is a domain-specific language to define and control hardware behaviour.
It is intened to run on embedded systems which are connected to motor controllers, sensors etc.

## Documentation

The documentation is available on https://lizard.dev.

## Prepare for Development

Install Python requirements:

```python
python3 -m pip install -r requirements.txt
```

Download [owl](https://github.com/ianh/owl), the language parser generator:

```bash
./get_owl.sh
```

Install UART drivers: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

Get all sub modules:

```bash
git submodule update --init --recursive
```

Generate the parser and compile Lizard:

```bash
./compile.sh
```

Flash the microcontroller:

```bash
./flash.py # if locally connected
./upload_ssh.sh <hostname> # if firmware should be deployed on a remote machine
```

Interact with the microcontroller using the serial monitor (does not yet work for remote machines):

```bash
./monitor.py
```

## Releasing

To build a new release, tag the commit with a "v" prefix, for example "v0.1.4".
A GitHub action will build the binary and create a new release.
After creation you can fill in a description if necessary.
