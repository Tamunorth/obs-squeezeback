@echo off
setlocal

echo ============================================
echo  OBS Squeezeback - Build ^& Install
echo ============================================
echo.

:: ── Find OBS installation ──
set "OBS_DIR="
for /f "tokens=2*" %%A in ('reg query "HKLM\SOFTWARE\OBS Studio" /ve 2^>nul') do set "OBS_DIR=%%B"
if not defined OBS_DIR (
    if exist "C:\Program Files\obs-studio\bin\64bit\obs.dll" set "OBS_DIR=C:\Program Files\obs-studio"
)
if not defined OBS_DIR (
    echo ERROR: Could not find OBS Studio installation.
    echo Set OBS_DIR manually and re-run.
    exit /b 1
)
echo Found OBS at: %OBS_DIR%

:: ── Set up VS environment ──
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: Could not set up Visual Studio environment.
    echo Install "Visual Studio Build Tools 2022" with C++ workload.
    exit /b 1
)

:: ── Generate import library if needed ──
if not exist "build\obs-lib" mkdir "build\obs-lib"
if not exist "build\obs-lib\obs.lib" (
    echo Generating obs.lib from obs.dll...
    dumpbin /exports "%OBS_DIR%\bin\64bit\obs.dll" > "build\obs-lib\obs_exports_raw.txt"

    echo LIBRARY obs > "build\obs-lib\obs.def"
    echo EXPORTS >> "build\obs-lib\obs.def"
    for /f "tokens=1,2,3,4" %%A in ('findstr /r "^[ ][ ]*[0-9]" "build\obs-lib\obs_exports_raw.txt"') do (
        if not "%%D"=="" echo     %%D >> "build\obs-lib\obs.def"
    )

    lib /def:"build\obs-lib\obs.def" /out:"build\obs-lib\obs.lib" /machine:x64 >nul
    if exist "build\obs-lib\obs.lib" (
        echo obs.lib created successfully.
    ) else (
        echo ERROR: Failed to create obs.lib
        exit /b 1
    )
)

:: ── Find CMake ──
set "CMAKE_EXE="
where cmake >nul 2>&1 && set "CMAKE_EXE=cmake"
if not defined CMAKE_EXE (
    if exist "C:\Program Files\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"
)
if not defined CMAKE_EXE (
    echo ERROR: CMake not found. Install via: winget install Kitware.CMake
    exit /b 1
)
echo Using CMake: %CMAKE_EXE%

:: ── Configure ──
echo.
echo Configuring...
"%CMAKE_EXE%" -S . -B build -G "Visual Studio 17 2022" -A x64 -DOBS_DIR="%OBS_DIR%" -Wno-dev
if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    exit /b 1
)

:: ── Build ──
echo.
echo Building...
"%CMAKE_EXE%" --build build --config Release
if errorlevel 1 (
    echo ERROR: Build failed.
    exit /b 1
)

:: ── Install to OBS ──
echo.
echo Installing to OBS...

set "PLUGIN_DIR=%OBS_DIR%\obs-plugins\64bit"
set "DATA_DIR=%OBS_DIR%\data\obs-plugins\obs-squeezeback"

copy /Y "build\Release\obs-squeezeback.dll" "%PLUGIN_DIR%\" >nul
if errorlevel 1 (
    echo ERROR: Could not copy DLL. Try running as Administrator.
    echo   Target: %PLUGIN_DIR%
    exit /b 1
)

if not exist "%DATA_DIR%" mkdir "%DATA_DIR%"
if not exist "%DATA_DIR%\locale" mkdir "%DATA_DIR%\locale"
copy /Y "data\squeezeback.effect" "%DATA_DIR%\" >nul
copy /Y "data\squeezeback_filter.effect" "%DATA_DIR%\" >nul
copy /Y "data\locale\en-US.ini" "%DATA_DIR%\locale\" >nul

echo.
echo ============================================
echo  SUCCESS!
echo ============================================
echo  DLL:    %PLUGIN_DIR%\obs-squeezeback.dll
echo  Data:   %DATA_DIR%\
echo.
echo  Restart OBS Studio, then select "Squeezeback"
echo  from the Scene Transitions dropdown.
echo ============================================

endlocal
