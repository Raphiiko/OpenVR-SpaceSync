REM Added by Shinyflvres, 2026-08-23. Part of SpaceSync, a modified version of OpenVR-SpaceOverride by Nyabsi (AGPL-3.0). See NOTICE.md
@echo off
setlocal EnableDelayedExpansion
REM ---------------------------------------------------------------------------
REM SpaceSync: build everything + create the NSIS installer.
REM
REM   build.bat            configure (if needed) + build + copy + installer
REM   build.bat clean      delete out\build\x64-release first, then do the above
REM   build.bat nopack     build + copy only, skip the installer
REM
REM Requirements: Visual Studio (C++ workload, ships CMake + Ninja), Vulkan SDK,
REM NSIS (makensis). Output: dev-resources\SpaceSync_Installer.exe
REM ---------------------------------------------------------------------------

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "PRESET=x64-release"
set "BUILD_DIR=%ROOT%\out\build\%PRESET%"
set "DO_CLEAN=0"
set "DO_PACK=1"

for %%a in (%*) do (
    if /I "%%a"=="clean"  set "DO_CLEAN=1"
    if /I "%%a"=="nopack" set "DO_PACK=0"
)

cd /d "%ROOT%"

REM --- Visual Studio developer environment (cl.exe, cmake, ninja) -------------
if not defined VSINSTALLDIR (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo [build] vswhere.exe not found - is Visual Studio installed?
        goto :fail
    )
    set "VSDIR="
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
    if not defined VSDIR (
        echo [build] No Visual Studio with the C++ toolset found.
        goto :fail
    )
    echo [build] Using Visual Studio: !VSDIR!
    call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" >nul
    if errorlevel 1 (
        echo [build] vcvars64.bat failed.
        goto :fail
    )
)

REM --- Vulkan SDK (needed by CMake: find_package(Vulkan REQUIRED)) ------------
if not defined VULKAN_SDK (
    for /d %%d in ("C:\VulkanSDK\*") do set "VULKAN_SDK=%%~d"
)
if not defined VULKAN_SDK (
    echo [build] Vulkan SDK not found. Install it: winget install KhronosGroup.VulkanSDK
    goto :fail
)
echo [build] Using Vulkan SDK: %VULKAN_SDK%

REM --- NSIS ---------------------------------------------------------------------
set "MAKENSIS="
if "%DO_PACK%"=="1" (
    where makensis >nul 2>nul && set "MAKENSIS=makensis"
    if not defined MAKENSIS if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
    if not defined MAKENSIS if exist "%ProgramFiles%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles%\NSIS\makensis.exe"
    if not defined MAKENSIS (
        echo [build] makensis.exe not found. Install NSIS: winget install NSIS.NSIS
        goto :fail
    )
    echo [build] Using NSIS: !MAKENSIS!
)

REM --- Submodules -----------------------------------------------------------------
if not exist "%ROOT%\3rdparty\OpenVR\headers\openvr.h" (
    echo [build] Submodules missing, running: git submodule update --init --recursive
    git submodule update --init --recursive
    if errorlevel 1 goto :fail
)

REM --- Configure + build ---------------------------------------------------------
if "%DO_CLEAN%"=="1" if exist "%BUILD_DIR%" (
    echo [build] Cleaning %BUILD_DIR%
    rmdir /s /q "%BUILD_DIR%"
)

if not exist "%BUILD_DIR%\build.ninja" (
    echo [build] Configuring preset %PRESET% ...
    cmake --preset %PRESET%
    if errorlevel 1 goto :fail
)

echo [build] Building ...
cmake --build "%BUILD_DIR%"
if errorlevel 1 goto :fail

REM --- Collect files where installer.nsi expects them ----------------------------
tasklist /FI "IMAGENAME eq SpaceSync.exe" 2>nul | findstr /I "SpaceSync.exe" >nul && (
    echo [build] Closing running SpaceSync so files can be replaced ...
    taskkill /IM SpaceSync.exe /F >nul 2>nul
    timeout /t 1 /nobreak >nul
)
echo [build] Copying overlay files to bin\ ...
if not exist "%ROOT%\bin" mkdir "%ROOT%\bin"
copy /Y "%BUILD_DIR%\SpaceSync.exe" "%ROOT%\bin\" >nul || goto :fail
copy /Y "%ROOT%\resources\LICENSE"             "%ROOT%\bin\" >nul || goto :fail
copy /Y "%ROOT%\resources\LICENSE.txt"         "%ROOT%\bin\" >nul || goto :fail
copy /Y "%ROOT%\NOTICE.md"                    "%ROOT%\bin\" >nul || goto :fail
copy /Y "%ROOT%\resources\LICENSES"            "%ROOT%\bin\" >nul || goto :fail
copy /Y "%ROOT%\resources\manifest.vrmanifest" "%ROOT%\bin\" >nul || goto :fail
copy /Y "%ROOT%\resources\icon.png"            "%ROOT%\bin\" >nul || goto :fail
if not exist "%ROOT%\bin\sound" mkdir "%ROOT%\bin\sound"
copy /Y "%ROOT%\src\sound\*.wav"               "%ROOT%\bin\sound\" >nul || goto :fail
if not exist "%ROOT%\bin\openvr_api.dll" (
    REM fall back to the OpenVR submodule's redistributable if bin\ does not have it
    copy /Y "%ROOT%\3rdparty\OpenVR\bin\win64\openvr_api.dll" "%ROOT%\bin\" >nul || goto :fail
)

echo [build] Copying driver to dev-resources\driver\bin\win64\ ...
if not exist "%ROOT%\dev-resources\driver\bin\win64" mkdir "%ROOT%\dev-resources\driver\bin\win64"
copy /Y "%BUILD_DIR%\driver_spacesync.dll" "%ROOT%\dev-resources\driver\bin\win64\" >nul || goto :fail
if exist "%ROOT%\dev-resources\driver\bin\win64\driver_spaceoverride.dll" del /Q "%ROOT%\dev-resources\driver\bin\win64\driver_spaceoverride.dll"

if "%DO_PACK%"=="0" (
    echo [build] Done ^(no installer requested^).
    goto :eof
)

REM --- Installer --------------------------------------------------------------------
echo [build] Building installer ...
pushd "%ROOT%\dev-resources"
"%MAKENSIS%" installer.nsi
set "NSIS_RC=%ERRORLEVEL%"
popd
if not "%NSIS_RC%"=="0" goto :fail

echo.
echo [build] OK: %ROOT%\dev-resources\SpaceSync_Installer.exe
goto :eof

:fail
echo.
echo [build] FAILED.
exit /b 1
