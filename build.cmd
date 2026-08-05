@echo off
title Building FallingWave 159...
echo Compiling FallingWave 159...

:: 1. Create a temporary Windows Resource file targeting your icon
echo IDI_ICON1 ICON "159hz.ico" > icon_resource.rc
windres icon_resource.rc -o icon_resource.o

:: 2. Compile the code (Using -mwindows to HIDE the terminal)
g++ main.cpp icon_resource.o -o "FallingWave159.exe" -O2 -w -I include/ -L lib/ -lraylib -lopengl32 -lgdi32 -lwinmm -static -static-libgcc -static-libstdc++ -mwindows

:: 3. Check for errors
if %errorlevel% neq 0 (
echo.
echo [ERROR] Build failed! Please read the compiler errors above.
pause
) else (
echo.
echo [SUCCESS] Build completed! FallingWave159.exe is ready.
)

:: 4. Clean up temporary files
del icon_resource.rc
del icon_resource.o