#!/usr/bin/env bash

echo "Generating parser..."
if [[ "language.owl" -nt src/parser.h ]]
then
    owl/owl -c language.owl -o src/parser.h
    if [[ $? -ne 0 ]]
    then
        rm -f src/parser.h 
        exit 1
    fi
else
    echo "Nothing to do."
fi

echo "Compiling Lizard..."
docker run --rm -v $PWD:/project -w /project espressif/idf:v4.2 make -j4 || exit 1
