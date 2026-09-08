@echo off
set "QT_BIN=C:\Qt5\5.15.2\mingw81_32\bin"
set "MINGW_BIN=C:\Qt5\Tools\mingw810_32\bin"
set "PATH=%QT_BIN%;%MINGW_BIN%;%PATH%"

cd /d "%~dp0\release"
if exist "codex_widget.exe" (
    start "" "codex_widget.exe"
) else (
    echo Binary not found. Building first...
    cd /d "%~dp0"
    call build.bat
    cd /d "%~dp0\release"
    if exist "codex_widget.exe" (
        start "" "codex_widget.exe"
    )
)
