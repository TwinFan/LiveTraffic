@ECHO OFF

REM We expect 2 parameters
IF "%~1"=="" GOTO :PRINT_HELP
IF "%~2"=="" GOTO :PRINT_HELP

SETLOCAL
SET MY_VC_BASE=%~1
SET MY_BUILD_DIR=%~2

REM Set VC environment variables
CALL "%MY_VC_BASE%\VC\Auxiliary\Build\vcvarsall.bat" x64
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

REM Create an empty directory to build in
IF exist "%MY_BUILD_DIR%\" RMDIR /Q /S "%MY_BUILD_DIR%"
MKDIR "%MY_BUILD_DIR%""
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%
PUSHD "%MY_BUILD_DIR%"
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

REM Create Makefile from CMakeLists.txt, using NMake output beacause NMake happens to be available
CMAKE -G "NMake Makefiles" -DCMAKE_BUILD_TYPE:STRING="RelWithDebInfo" -DCMAKE_MAKE_PROGRAM="nmake.exe" -DCMAKE_TOOLCHAIN_FILE="..\docker\Toolchain-msvc-x86-64.cmake" ..
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

REM Perform the actual build
NMAKE /A
IF %ERRORLEVEL% NEQ 0 EXIT /B %ERRORLEVEL%

ECHO Windows Build: SUCCESS!
POPD
EXIT /B 0

:PRINT_HELP
ECHO Call with two parameters:
ECHO 1. Base directory for Visual Studio Build Tools, something like
ECHO    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community" or
ECHO    "C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools";
ECHO 2. Build directory, usually something local like "build-win".
EXIT /B 1
