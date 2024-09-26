#!/usr/bin/env bash

./gen_parser.sh

echo "Compiling Lizard..."
docker run --rm -v $PWD:/project -w /project espressif/idf:v5.3.1 idf.py build || exit 1
