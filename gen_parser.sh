#!/bin/bash

echo "This generator is does not include changes from PR #107 and should not be used."
exit 1

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

    # remove minimum size of 4096 bytes (see https://github.com/zauberzeug/lizard/issues/23)
    sed -i '' 's/while (n < size \|\| n < 4096)/while (n < size)/g' main/parser.h

    # increase RESERVATION_AMOUNT to 11 (see https://github.com/zauberzeug/field_friend/issues/7)
    sed -i '' 's/#define RESERVATION_AMOUNT 10/#define RESERVATION_AMOUNT 11/g' main/parser.h
else
    echo "Nothing to do."
fi
