@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo  Packaging Codex Monitor Widget (CMW) into deploy\
echo ===================================================

cd /d "%~dp0"

echo [1/3] Ensuring release build is up to date...
call build.bat
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed!
    exit /b %ERRORLEVEL%
)

echo [2/3] Assembling portable distribution folder...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "if (Test-Path deploy) { Remove-Item deploy -Recurse -Force };" ^
    "New-Item -ItemType Directory -Path deploy | Out-Null;" ^
    "Copy-Item release\codex_widget.exe deploy\;" ^
    "Copy-Item qt.conf deploy\;" ^
    "Copy-Item LICENSE deploy\;" ^
    "Copy-Item deploy_readme.txt deploy\README.txt;" ^
    "Get-ChildItem release -Filter *.dll | ForEach-Object { Copy-Item $_.FullName deploy\ };" ^
    "foreach ($folder in @('platforms', 'styles', 'iconengines', 'imageformats')) { if (Test-Path ('release\' + $folder)) { Copy-Item ('release\' + $folder) ('deploy\' + $folder) -Recurse -Force } }"

echo [3/3] Creating distribution ZIP archive...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "if (Test-Path CodexMonitorWidget-v1.0-Windows.zip) { Remove-Item CodexMonitorWidget-v1.0-Windows.zip -Force };" ^
    "Compress-Archive -Path deploy\* -DestinationPath CodexMonitorWidget-v1.0-Windows.zip -Force"

echo ===================================================
echo  Deployment complete!
echo  Folder:  deploy\
echo  Archive: CodexMonitorWidget-v1.0-Windows.zip
echo ===================================================
