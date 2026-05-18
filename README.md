# UA_source
![icon image](https://github.com/Marisa-Chan/UA_source/blob/master/svg/icon.svg?raw=true)

Opensource reimplementation of **UrbanAssault** engine. You needed copy of original game files for play.

**License** GPLv2

# Building UA Source on Modern Windows (64-bit MSYS2):

1. Install MSYS2
Download and install MSYS2:
https://www.msys2.org/

2. Open MSYS2 MSYS
Launch:
MSYS2 MSYS

3. Update MSYS2
Run:
pacman -Syu
If MSYS2 asks you to close the terminal, close it and reopen it before continuing.

4. Install all required dependencies
Run:
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-SDL2_ttf mingw-w64-x86_64-SDL2_net mingw-w64-x86_64-openal mingw-w64-x86_64-libvorbis mingw-w64-x86_64-ffmpeg mingw-w64-x86_64-lua

5. Download the source code
Download the source code ZIP from:
https://github.com/TeuZzZ-17/UA_source

6. Extract the project
Extract the UA_source folder to your Desktop.
Example:
C:\Users\YourName\Desktop\UA_source-master

7. Open the MinGW64 environment
Launch:
MSYS2 MinGW 64-bit
or directly:
C:\msys64\mingw64.exe

8. Go to the project folder
Example:
cd /c/Users/YourName/Desktop/UA_source-master

9. Configure the project
Run:
cmake -B build -S src

10. Compile the project
Run:
cmake --build build -j12

11. Compiled executable
If compilation succeeds, you will find:
build/UA_source.exe

12. Obtain an original copy of Urban Assault
Use a clean, unmodified installation of the original game.

13. Copy required files into the original game folder
Copy the following into your Urban Assault installation folder:
UA_source.exe
res/
fonts/
locale/language.lng

14. Missing DLLs at startup
If UA_source.exe reports missing DLL files at startup:
Copy the required DLLs from:
C:\msys64\mingw64\bin
into the same folder as:
UA_source.exe

15. Check which DLLs are actually required
Inside mingw64.exe, run:
ldd build/UA_source.exe
This shows all runtime dependencies used by the executable.

16. Automatically copy all required MinGW64 DLLs
Instead of manually copying DLLs one by one, you can automatically copy all required MinGW64 runtime DLLs into the build folder using:
ldd build/UA_source.exe | grep mingw64/bin | awk '{print $3}' | xargs -I {} cp -v {} build/
This command:
    • scans all executable dependencies,
    • filters only MinGW64 runtime DLLs,
    • extracts their file paths,
    • automatically copies them into:
build/
This avoids copying hundreds of unnecessary DLLs like many old 32-bit guides do.
After this step, build/UA_source.exe should be portable and runnable outside the MSYS2 environment.

-----------------------------------------------------------------------------------------------------------------

**Fonts** directory contains:
- Liberation Mono, Liberation Sans, Liberation Serif.  Version 2.00.1
- Xolonium
- Press Start 2P
- Textar


**Licenses of used fonts**:
- The Liberation(tm) version 2.00.0 onward are Licensed under the SIL Open Font License, Version 1.1.
- Xolonium by Severin Meyer. License: SIL Open Font License (OFL)
- Press Start 2P by Cody 'CodeMan38' Boisclair. License: SIL Open Font License (OFL)
- Textar (https://github.com/yamacraft/textar-font) License: IPA-1


**Additional keys (nucleus.ini)**:
- gfx.blending   0(default)/1/2 - blending mode for transparent things. 0 - default multiply, 1 - additive, 2 - sharp edged
- gfx.solidfont   on/off(default) - enable(on) pointy font rasterizing without of pixel blending
- gfx.vsync 0/1(default)/2 - vertical sync type. 0 - disable, 1 - default sync, 2 - adaptive(if you've already missed the vertical retrace for a given frame, it swaps buffers immediately)
- gfx.maxfps 60(default) - maximal fps. Strongly recommended do not up it more than 60 - currently it will break helicopters rotations and AI
- gfx.newsky on/off(default) - enable new fade effect of far grounds
- gfx.skydistance 3000(default) - start distance of sky fade effect (do not rise it too strong, because of limits of sky sphere)
- gfx.skylength 500(default) - length of sky fade effect
- gfx.color_effects 0(default)/1/2 - emulate color effects like in original game in software rendering. 0 - disable, 1 - affects all, 2 - do not affect GUI
- gfx.color_eff_pwr[1] 0-100(default) - power of red color effect
- gfx.color_eff_pwr[2] 0-100(default) - power of blue color effect
- gfx.color_eff_pwr[3] 0-100(default) - power of green color effect
- gfx.color_eff_pwr[4] 0-100(default) - power of inverse color effect
- gfx.color_eff_pwr[5] 0-100(default) - power of invdark color effect
- gfx.color_eff_pwr[6] 0-100(default) - power of sw color effect
- gfx.color_eff_pwr[7] 0-100(default) - power of invtuerk color effect
- particles.limit 5000(default) - limit amount of particles 
- menu.windowed on/off(default) - always open game menu in windowed mode
- gfx.vbo on(default)/off - disable modern VBO(upload vertex data into video card)



- world.ini : begin_misc : fix_weapon_radius - on/off(default) - enable(on) fix of original bug that prevent working of different weapon radius
- world.ini : begin_misc : hidden_fractions - mask for hidden fractions for whole world
- levelfile : begin_level : hidden_fractions - mask for hidden fractions for this level
- vehicle : hidden (yes/no) - hide this unit type
- vehicle : unhide_radar (0 disable) - unhide number of sectors

