@echo off
setlocal

echo Searching for Visual Studio C++ Compiler...

:: User provided path (VS 2026 / v18)
set "VS_USER_PATH=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

if exist "%VS_USER_PATH%" (
    call "%VS_USER_PATH%"
    goto :Build
)

:: Common paths for vcvars64.bat
set "VS2026_COMMUNITY=C:\Program Files\Microsoft Visual Studio\2026\Community\VC\Auxiliary\Build\vcvars64.bat"
set "VS2026_PRO=C:\Program Files\Microsoft Visual Studio\2026\Professional\VC\Auxiliary\Build\vcvars64.bat"
set "VS2026_ENT=C:\Program Files\Microsoft Visual Studio\2026\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
set "VS2026_PRE=C:\Program Files\Microsoft Visual Studio\2026\Preview\VC\Auxiliary\Build\vcvars64.bat"

if exist "%VS2026_COMMUNITY%" (
    call "%VS2026_COMMUNITY%"
    goto :Build
)
if exist "%VS2026_PRO%" (
    call "%VS2026_PRO%"
    goto :Build
)
if exist "%VS2026_ENT%" (
    call "%VS2026_ENT%"
    goto :Build
)
if exist "%VS2026_PRE%" (
    call "%VS2026_PRE%"
    goto :Build
)

echo.
echo WARNING: Could not automatically find Visual Studio 2026.
echo Attempting to check for VS 2019...

set "VS2019_COMMUNITY=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist "%VS2019_COMMUNITY%" (
    call "%VS2019_COMMUNITY%"
    goto :Build
)

:: Fallback advice
echo.
echo ERROR: Could not find 'vcvars64.bat'.
echo Please run this script from the "Developer Command Prompt for Visual Studio".
echo.
exit /b 1

:Build
echo.
echo Environment set. Building MiniR...
echo.

:: Compile with exception handling enabled (/EHsc)
:: /std:c++20 to match the Makefile
:: /I. to include current directory for headers
cl /EHsc /std:c++20 /O2 /I. cli_main.cpp runtime\Evaluator.cpp runtime\Parser.cpp runtime\Lexer.cpp ^
   runtime\BuiltinMath.cpp runtime\BuiltinLogical.cpp runtime\BuiltinString.cpp ^
   runtime\BuiltinRandom.cpp runtime\BuiltinVector.cpp runtime\BuiltinSubset.cpp ^
   runtime\BuiltinSelection.cpp /Fe:minir.exe

if %errorlevel% neq 0 (
    echo.
    echo Build FAILED.
    exit /b %errorlevel%
)

echo.
echo Build SUCCESS! Run minir.exe to start.
