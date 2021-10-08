# Lizard

Lizard is a domain-specific language to define and control hardware behaviour. It is intened to run on embedded systems which are connected to motor controllers, sensors etc.

## Getting started

    python3 -m pip install -r requirements.txt

## Documentation

Launch the documentation locally with

```bash
mkdocs serve
```

Eventually we will publish these on https://lizard.dev through GitHub Pages. But for now you can deploy it with

```bash
mkdocs build && rsync -aluv --delete site/* loop:public-web/lizard/
```