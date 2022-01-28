#!/usr/bin/env bash

echo "Generating parser..."
if [[ "language.owl" -nt main/parser.h ]]
then
    owl/owl -c language.owl -o main/parser.h
    if [[ $? -ne 0 ]]
    then
        rm -f main/parser.h 
        exit 1
    fi
else
    echo "Nothing to do."
fi

echo "Compiling Lizard..."
docker run --rm -v $PWD:/project -w /project espressif/idf:v4.4 idf.py build || exit 1
