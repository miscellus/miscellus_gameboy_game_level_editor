#!/bin/bash

set -e

compile_flags=("
-O0 
-ggdb
-Wall
-Wextra
-pedantic
-std=c99
-I/usr/include
$(pkg-config --cflags gtk+-3.0)
")

link_flags=("
-L/usr/lib
-lSDL2 -Wl,-rpath=/usr/lib
-lm
$(pkg-config --libs gtk+-3.0)
")

gcc $compile_flags level_editor.c -o level_editor.program $link_flags

./level_editor.program level_editor.program
# termite -e "bash -c './level_editor.program'"