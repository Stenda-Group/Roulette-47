@echo off
rem Compile le mod (Release) et, avec --installer, l installe dans Retail (version.dll, Roulette47).
rem Le jeu doit être fermé pour installer : la DLL est verrouillée tant qu'il tourne.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0.."
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build || exit /b 1
if "%1"=="--installer" cmake --install build || exit /b 1
echo Compilation terminee.
