#!/usr/bin/env bash

./gen_parser.sh

echo "Compiling Lizard..."
docker run --rm -v $PWD:/project -w /project espressif/idf:v4.4 idf.py build || exit 1
