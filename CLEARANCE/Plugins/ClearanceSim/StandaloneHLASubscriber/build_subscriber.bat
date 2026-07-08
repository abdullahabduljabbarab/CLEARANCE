@echo off
:: ============================================================================
:: build_subscriber.bat - compile clearance_hla_subscriber.exe standalone.
::
:: Needs:
::  - Visual Studio 2022 or 2026 with C++ Build Tools installed
::  - The vendored OpenRTI SDK at ../ThirdParty/OpenRTI/ (headers + libs +
::    DLLs; already checked in alongside the plugin)
::
:: Run this file (no arguments) - it locates vcvars64.bat, sets the MSVC
:: environment, then invokes cl.exe against the OpenRTI import libraries.
:: Produces clearance_hla_subscriber.exe in this folder + copies the three
:: OpenRTI DLLs next to it so the exe launches with no PATH setup.
:: - TripleA
:: ============================================================================

setlocal

set "OPENRTI_ROOT=%~dp0..\ThirdParty\OpenRTI"
if not exist "%OPENRTI_ROOT%\lib\rti1516e.lib" (
    echo ERROR: vendored OpenRTI not found at "%OPENRTI_ROOT%"
    echo Rebuild from source and drop outputs at include/lib/bin.
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

set "OUT_EXE=%~dp0clearance_hla_subscriber.exe"
set "SRC_MAIN=%~dp0subscriber_main.cpp"

echo Compiling %SRC_MAIN%

:: /Zc:__cplusplus so rti1516e headers pick std::unique_ptr (not auto_ptr).
:: /EHsc for exception unwind semantics required by the RTI headers.
:: /std:c++17 for structured bindings + inline variables. - TripleA
cl.exe /nologo /EHsc /std:c++17 /Zc:__cplusplus /MD /O2 ^
    /I "%OPENRTI_ROOT%\include\rti1516e" ^
    "%SRC_MAIN%" ^
    /link ^
    "%OPENRTI_ROOT%\lib\rti1516e.lib" ^
    "%OPENRTI_ROOT%\lib\fedtime1516e.lib" ^
    "%OPENRTI_ROOT%\lib\OpenRTI.lib" ^
    /OUT:"%OUT_EXE%"

if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

:: Copy the three OpenRTI DLLs so the exe finds them without PATH setup.
copy /Y "%OPENRTI_ROOT%\bin\OpenRTI.dll"        "%~dp0" >nul
copy /Y "%OPENRTI_ROOT%\bin\librti1516e.dll"    "%~dp0" >nul
copy /Y "%OPENRTI_ROOT%\bin\libfedtime1516e.dll" "%~dp0" >nul

echo.
echo Build OK: %OUT_EXE%
echo Usage: clearance_hla_subscriber.exe [federation] [federate] [fom-path]
echo Defaults: CLEARANCE / CLEARANCE-Subscriber / ..\FOM\ClearanceRPR-FOM.xml
