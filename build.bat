
@SET compile_flags=-O0 -Wall -Wextra -pedantic -std=c99 -I.\SDL2-2.0.12\x86_64-w64-mingw32\include
@SET link_flags=-L.\SDL2-2.0.12\x86_64-w64-mingw32\lib -w -Wl,-subsystem,windows -lmingw32 -lSDL2main -lSDL2 -lm -lComdlg32

gcc %compile_flags% level_editor.c -o level_editor.exe %link_flags%

level_editor.exe level_editor.exe