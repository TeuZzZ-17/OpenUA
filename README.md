# OpenUA

Opensource reimplementation of **UrbanAssault** engine. You needed copy of original game files for play.

**License** GPLv2

# Building OpenUA on Modern Windows (64-bit MSYS2):

1. Download and install MSYS2:
https://www.msys2.org/

2. Open MSYS2 MSYS

3. Run:
pacman -Syu for updating.
If MSYS2 asks you to close the terminal, close it and reopen it before continuing.

4. Install all required dependencies:
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-SDL2_ttf mingw-w64-x86_64-SDL2_net mingw-w64-x86_64-openal mingw-w64-x86_64-libvorbis mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-lua

5. Download the source code ZIP from:
https://github.com/TeuZzZ-17/UA_source

6. Extract the UA_source folder to your Desktop.
Example:
C:\Users\YourName\Desktop\UA_source-experimental

7. Open the MinGW64 environment with MSYS2 MinGW 64-bit
or directly:
C:\msys64\mingw64.exe

8. In MinGW64 go to the project folder
Example:
cd /c/Users/YourName/Desktop/UA_source-experimental

9. Configure the project:
cmake -B build -S src

10. Compile the project:
cmake --build build -j12

11. If compilation succeeds, you will find:
build/UA_source.exe

12. Obtain an original copy of Urban Assault:
Use a clean, unmodified installation of the original game.

13. Copy the following into your Urban Assault installation folder:
    
UA_source.exe,
res,
fonts,
locale/language.lng

15. If UA_source.exe reports missing DLL files at startup:
Copy the required DLLs from:
C:\msys64\mingw64\bin
into the same folder as:
UA_source.exe

16. After this step, build/UA_source.exe should be portable and runnable outside the MSYS2 environment.
