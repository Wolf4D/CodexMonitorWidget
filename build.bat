@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo  Building Codex Widget (C++ / Qt 5.15.2 MinGW)
echo ===================================================

set "QT_BIN=C:\Qt5\5.15.2\mingw81_32\bin"
set "MINGW_BIN=C:\Qt5\Tools\mingw810_32\bin"

set "PATH=%QT_BIN%;%MINGW_BIN%;%PATH%"

cd /d "%~dp0"

echo [1/4] Compiling translations with lrelease...
"%QT_BIN%\lrelease.exe" codex_widget.pro
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] lrelease failed!
    exit /b %ERRORLEVEL%
)

echo [2/4] Running qmake...
"%QT_BIN%\qmake.exe" codex_widget.pro -spec win32-g++ "CONFIG+=release"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] qmake failed!
    exit /b %ERRORLEVEL%
)

echo [3/4] Compiling with mingw32-make...
"%MINGW_BIN%\mingw32-make.exe" -f Makefile.Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Compilation failed!
    exit /b %ERRORLEVEL%
)

echo [4/4] Deploying Qt dependencies...
if exist "release\codex_widget.exe" (
    "%QT_BIN%\windeployqt.exe" release\codex_widget.exe --no-translations --no-system-d3d-compiler
    echo ===================================================
    echo  Build Successful! Output: release\codex_widget.exe
    echo ===================================================
) else (
    echo [ERROR] release\codex_widget.exe not found!
    exit /b 1
)
