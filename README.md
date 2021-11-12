# Lizard

Lizard is a domain-specific language to define and control hardware behaviour.
It is intened to run on embedded systems which are connected to motor controllers, sensors etc.

## Getting started

Install Python requirements:

    python3 -m pip install -r requirements.txt

Download [owl](https://github.com/ianh/owl), the language parser generator:

    ./get_owl.sh

Generate the parser, compile Lizard and flash the microcontroller:

    ./upload.sh

Interact with the microcontroller using the serial monitor:

    ./monitor.py

## Documentation

Launch the documentation locally with

```bash
mkdocs serve
```

Eventually we will publish these on https://lizard.dev through GitHub Pages.
But for now you can deploy it with

```bash
mkdocs build && rsync -aluv --delete site/* root@lizard.dev:public-web/lizard/
```
