@echo off
:: ============================================================================
:: build_subscriber.bat - compile clearance_dds_subscriber.exe standalone.
::
:: Needs:
::  - Visual Studio 2022 or 2026 with C++ Build Tools installed
::  - eProsima Fast DDS SDK at C:\Program Files\eProsima\fastdds 3.6.1.0
::    (or set FASTDDS_HOME env var to override)
::
:: Run this file (no arguments) - it locates vcvars64.bat, sets the MSVC
:: environment, then invokes cl.exe with the same static link line the
:: ClearanceDDS UE module uses. Produces clearance_dds_subscriber.exe
:: in the same folder. - TripleA
:: ============================================================================

setlocal

if "%FASTDDS_HOME%"=="" set "FASTDDS_HOME=C:\Program Files\eProsima\fastdds 3.6.1.0"
if not exist "%FASTDDS_HOME%\include\fastdds" (
    echo ERROR: Fast DDS SDK not found at "%FASTDDS_HOME%"
    echo Set FASTDDS_HOME env var or install the SDK at the default location.
    exit /b 1
)

:: Find vcvars64.bat. Prefers vswhere from Visual Studio Installer.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found - install Visual Studio Build Tools.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_INSTALL_PATH=%%i"
)
if "%VS_INSTALL_PATH%"=="" (
    echo ERROR: no Visual Studio install with C++ tools found.
    exit /b 1
)

call "%VS_INSTALL_PATH%\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

:: Build.
set "OUT_EXE=%~dp0clearance_dds_subscriber.exe"
set "SRC_MAIN=%~dp0subscriber_main.cpp"
:: Also compile the fastddsgen output that defines the PubSubType classes
:: - they're referenced from subscriber_main but only declared in the .hpp,
:: bodies live in this .cpp. - TripleA
set "SRC_PUBSUB=%~dp0..\Source\ClearanceDDS\Private\Generated\AirspaceTelemetryPubSubTypes.cpp"

echo Compiling %SRC_MAIN%
echo Compiling %SRC_PUBSUB%
echo Output:    %OUT_EXE%
echo.

cl.exe /nologo /std:c++17 /EHsc /MD /O2 ^
    "/I%FASTDDS_HOME%\include" ^
    "/I%~dp0..\Source\ClearanceDDS\Private\Generated" ^
    "%SRC_MAIN%" ^
    "%SRC_PUBSUB%" ^
    /Fe:"%OUT_EXE%" ^
    /link ^
    "/LIBPATH:%FASTDDS_HOME%\lib" ^
    "/LIBPATH:%FASTDDS_HOME%\lib\VC\x64\MD" ^
    libfastdds-3.6.lib ^
    libfastcdr-2.3.lib ^
    foonathan_memory-0.7.4.lib ^
    libcrypto.lib ^
    libssl.lib ^
    ws2_32.lib ^
    iphlpapi.lib ^
    shlwapi.lib ^
    crypt32.lib ^
    advapi32.lib ^
    user32.lib

if errorlevel 1 (
    echo.
    echo BUILD FAILED
    exit /b 1
)

echo.
echo BUILD OK - %OUT_EXE%
endlocal
