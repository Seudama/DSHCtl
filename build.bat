@echo off
chcp 65001 > NUL
setlocal enabledelayedexpansion

:: ============================================
::  DSHCtl One-Click Build
::  Usage: build.bat [release] [deploy] [run]
:: ============================================

:: ---- Auto-detect VS2022 ----
set "VS2022_PATH="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>NUL`) do (
        if not "%%i"=="" set "VS2022_PATH=%%i"
    )
)
if not defined VS2022_PATH set "VS2022_PATH=D:\Softwares\VS2022"

:: ---- Auto-detect Qt6 ----
set "QT6_PATH="
if defined QTDIR (if exist "%QTDIR%\bin\qmake.exe" set "QT6_PATH=%QTDIR%")
if not defined QT6_PATH (
    for /f "usebackq tokens=*" %%d in (`dir /b /ad /on "D:\Softwares\QT6\6.*" 2^>NUL`) do (
        if exist "D:\Softwares\QT6\%%d\msvc2022_64\bin\qmake.exe" set "QT6_PATH=D:\Softwares\QT6\%%d\msvc2022_64"
    )
)

set "PROJECT_ROOT=%~dp0"
set "BUILD_DIR=%PROJECT_ROOT%vs-build"
set "BIN_DIR=%PROJECT_ROOT%bin"

:: ---- Args ----
set "BUILD_CONFIG=RelWithDebInfo"
set "RUN_AFTER=0"
set "DEPLOY=0"

:parse_args
if "%~1"=="" goto :done_parsing
if /i "%~1"=="release" set "BUILD_CONFIG=Release"
if /i "%~1"=="run" set "RUN_AFTER=1"
if /i "%~1"=="deploy" set "DEPLOY=1"
shift
goto :parse_args
:done_parsing

:: ---- MSVC ----
if not exist "%VS2022_PATH%\VC\Auxiliary\Build\vcvarsall.bat" (
    echo [ERROR] VS2022 not found: %VS2022_PATH%
    pause
    exit /b 1
)
call "%VS2022_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 > NUL 2>&1
if errorlevel 1 (
    echo [ERROR] MSVC init failed
    pause
    exit /b 1
)

:: ---- Qt6 ----
if not exist "%QT6_PATH%\bin\qmake.exe" (
    echo [ERROR] Qt6 not found: %QT6_PATH%
    pause
    exit /b 1
)
set "QTDIR=%QT6_PATH%"
set "CMAKE_PREFIX_PATH=%QT6_PATH%"
set "PATH=%QT6_PATH%\bin;%PATH%"

:: ---- Kill running instance ----
taskkill /F /IM DSHCtl.exe > NUL 2>&1

echo.
echo ========================================
echo   DSHCtl Build: %BUILD_CONFIG%
echo ========================================
echo.

:: ---- Clean ----
echo [1/4] Clean...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

:: ---- CMake ----
echo [2/4] CMake configure...
pushd "%BUILD_DIR%"
cmake -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=%BUILD_CONFIG% "%PROJECT_ROOT:~0,-1%"
if errorlevel 1 (
    echo [ERROR] CMake failed
    popd
    pause
    exit /b 1
)

:: ---- Build ----
echo [3/4] Build...
cmake --build . --config %BUILD_CONFIG%
if errorlevel 1 (
    echo [ERROR] Build failed
    popd
    pause
    exit /b 1
)
popd

:: ---- Deploy ----
if "%DEPLOY%"=="1" (
    echo [4/4] Deploy Qt runtime...
    "%QT6_PATH%\bin\windeployqt.exe" --release --compiler-runtime --no-translations --no-opengl-sw "%BIN_DIR%\DSHCtl.exe"
    if errorlevel 1 (
        echo [ERROR] windeployqt failed
        pause
        exit /b 1
    )
) else (
    echo [4/4] Deploy skipped - add "deploy" arg to bundle Qt DLLs
)

echo.
echo [OK] Build succeeded!

:: ---- Run ----
if "%RUN_AFTER%"=="1" (
    if exist "%BIN_DIR%\DSHCtl.exe" (
        echo [INFO] Starting DSHCtl.exe...
        start "" "%BIN_DIR%\DSHCtl.exe"
    ) else (
        echo [ERROR] DSHCtl.exe not found
    )
)

endlocal
exit /b 0
