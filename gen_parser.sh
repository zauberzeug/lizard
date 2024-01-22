#!/bin/bash

echo "Generating parser..."
if [[ "language.owl" -nt main/parser.h ]]
then
    pushd owl
    make
    popd
    mv owl/owl ./owl_executable

    ./owl_executable -c language.owl -o main/parser.h
    if [[ $? -ne 0 ]]
    then
        rm -f main/parser.h
        exit 1
    fi

    # remove minimum size of 4096 bytes
    sed -i '' 's/while (n < size \|\| n < 4096)/while (n < size)/g' main/parser.h
else
    echo "Nothing to do."
fi
