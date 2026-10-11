@echo off
title STM32H750 BootManager Launcher
cd /d "%~dp0"

echo ============================================================
echo   Launching STM32H750 Dual-Slot Bootloader GUI (Tauri 2.0)...
echo ============================================================

if exist "src-tauri\target\debug\bootloader_gui.exe" (
    echo [OK] Launching pre-compiled desktop executable...
    start "" "src-tauri\target\debug\bootloader_gui.exe"
) else (
    echo [INFO] Building and starting in dev mode...
    npm run tauri dev
)
