@echo off
title Building FallingWave 159...
echo Compiling FallingWave 159...

:: 1. Create a temporary Windows Resource file targeting your icon
echo IDI_ICON1 ICON "159hz.ico" > icon_resource.rc

:: 2. Compile the resource file using windres (comes with g++)
windres icon_resource.rc -o icon_resource.o

:: 3. Compile the code and link the compiled icon object (-o FallingWave159.exe)
g++ main.cpp icon_resource.o -o "FallingWave159.exe" -O2 -w -I include/ -L lib/ -lraylib -lopengl32 -lgdi32 -lwinmm

:: 4. Clean up the temporary resource files so your folder stays clean
del icon_resource.rc
del icon_resource.o

:: Check if the compilation failed (errorlevel is not 0)
if %errorlevel% neq 0 (