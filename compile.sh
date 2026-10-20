#!/usr/bin/env bash

./gen_parser.sh

echo "Compiling Lizard..."
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD:/project" -w /project \
    espressif/idf:v5.3.1 idf.py build || exit 1
