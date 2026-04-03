@echo off
setlocal

echo ============================================
echo  OBS Squeezeback - Release Build
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

:: ── Generate frontend API import library if needed ──
if not exist "build\obs-lib\obs-frontend-api.lib" (
    echo Generating obs-frontend-api.lib...
    dumpbin /exports "%OBS_DIR%\bin\64bit\obs-frontend-api.dll" > "build\obs-lib\frontend_exports_raw.txt"

    echo LIBRARY obs-frontend-api > "build\obs-lib\obs-frontend-api.def"
    echo EXPORTS >> "build\obs-lib\obs-frontend-api.def"
    for /f "tokens=1,2,3,4" %%A in ('findstr /r "^[ ][ ]*[0-9]" "build\obs-lib\frontend_exports_raw.txt"') do (
        if not "%%D"=="" echo     %%D >> "build\obs-lib\obs-frontend-api.def"
    )

    lib /def:"build\obs-lib\obs-frontend-api.def" /out:"build\obs-lib\obs-frontend-api.lib" /machine:x64 >nul
    if exist "build\obs-lib\obs-frontend-api.lib" (
        echo obs-frontend-api.lib created successfully.
    ) else (
        echo ERROR: Failed to create obs-frontend-api.lib
        exit /b 1
    )
)

:: ── Find Qt6 ──
set "QT6_DIR="
if exist "C:\Qt\6.7.0\msvc2019_64" set "QT6_DIR=C:\Qt\6.7.0\msvc2019_64"
if not defined QT6_DIR (
    if exist "C:\Qt\6.7.0\msvc2022_64" set "QT6_DIR=C:\Qt\6.7.0\msvc2022_64"
)
if not defined QT6_DIR (
    for /f "tokens=*" %%i in ('dir /b /ad "C:\Qt\6.*" 2^>nul') do (
        for /f "tokens=*" %%j in ('dir /b /ad "C:\Qt\%%i\msvc*_64" 2^>nul') do (
            set "QT6_DIR=C:\Qt\%%i\%%j"
        )
    )
)
if not defined QT6_DIR (
    echo ERROR: Qt6 not found. Install via: pip install aqtinstall ^&^& aqt install-qt windows desktop 6.7.0 win64_msvc2019_64 -O C:\Qt
    exit /b 1
)
echo Found Qt6 at: %QT6_DIR%

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
"%CMAKE_EXE%" -S . -B build -G "Visual Studio 17 2022" -A x64 -DOBS_DIR="%OBS_DIR%" -DCMAKE_PREFIX_PATH="%QT6_DIR%" -Wno-dev
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

:: ── Find Inno Setup ──
set "ISCC="
where iscc >nul 2>&1 && set "ISCC=iscc"
if not defined ISCC (
    if exist "C:\Users\davie\AppData\Local\Programs\Inno Setup 6\ISCC.exe" set "ISCC=C:\Users\davie\AppData\Local\Programs\Inno Setup 6\ISCC.exe"
)
if not defined ISCC (
    if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
)
if not defined ISCC (
    if exist "C:\Program Files\Inno Setup 6\ISCC.exe" set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"
)
if not defined ISCC (
    echo ERROR: Inno Setup not found.
    echo Install via: winget install JRSoftware.InnoSetup
    exit /b 1
)

:: ── Create installer ──
echo.
echo Creating installer...
if not exist "dist" mkdir "dist"
"%ISCC%" installer.iss
if errorlevel 1 (
    echo ERROR: Installer creation failed.
    exit /b 1
)

echo.
echo ============================================
echo  RELEASE BUILD COMPLETE
echo ============================================
echo  Installer: %CD%\dist\obs-squeezeback-1.0.0-setup.exe
echo ============================================

endlocal
