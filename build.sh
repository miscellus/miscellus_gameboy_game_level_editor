#!/bin/bash

set -e
# gcc -Wall -Wextra -std=c99 metaprogram.c -o meta.program
# ./meta.program main.c
#pushd meta
gcc -Wall -Wextra -std=c99 -I/usr/include main.c -L/usr/lib -lSDL2 -Wl,-rpath=/usr/lib -o testsdl.program -lm
#popd

./testsdl.program