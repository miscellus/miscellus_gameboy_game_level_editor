#!/bin/bash

set -e
# gcc -Wall -Wextra -std=c99 metaprogram.c -o meta.program
# ./meta.program main.c
#pushd meta
gcc -Wall -Wextra -pedantic -std=c99 -I/usr/include level_editor.c -o level_editor.program -L/usr/lib -lSDL2 -Wl,-rpath=/usr/lib -lm
#popd

./level_editor.program