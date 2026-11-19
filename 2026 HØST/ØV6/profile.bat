@echo off
setlocal

@REM Usage: profile.bat [input-file]   (default: Twenty_thousand_leagues_under_the_sea.txt)
@REM Needs: samply and python on PATH, fixnames.py in this folder.

@REM * Settings 
set "EXE=compress-profiling.exe"
set "INPUT=%~1"
if "%INPUT%"=="" set "INPUT=Twenty_thousand_leagues_under_the_sea.txt"
set "MSYS_BIN=C:\msys64\ucrt64\bin"
set "RAW=profile.json.gz"
set "FIXED=%~n1"
if "%FIXED%"=="" set "FIXED=Twenty_thousand_leagues_under_the_sea"
set "FIXED=%FIXED%.fixed.json.gz"

@REM addr2line lives in the MSYS2 toolchain; add it for this script only
set "PATH=%PATH%;%MSYS_BIN%"

@REM * 1. Record a sampling profile (no browser opens) 
echo [1/3] Recording profile of %EXE% %INPUT%
samply record --save-only -o "%RAW%" ".\%EXE%" "%INPUT%"
if errorlevel 1 exit /b 1

@REM * 2. Resolve function names from the exe (MinGW has DWARF, samply wants PDB) 
echo [2/3] Resolving function names
python fixnames.py "%RAW%" "%EXE%" "%FIXED%"
if errorlevel 1 exit /b 1

@REM * 3. Open the result in the Firefox Profiler (Ctrl+C to stop the server)
echo [3/3] Opening %FIXED%
samply load "%FIXED%"

endlocal