@echo off
rem Runs a command inside the 64-bit MSVC build environment, plus the PATH
rem additions (Qt, Cargo) that setup-windows.ps1 recorded in local-env.cmd.
rem
rem   tools\dev-env\msvc.cmd cmake --preset dev
rem   tools\dev-env\msvc.cmd cmake --build --preset dev
rem   tools\dev-env\msvc.cmd ctest --preset dev
rem   tools\dev-env\msvc.cmd build\dev\brazenworks.exe

setlocal

if "%~1"=="" (
  echo usage: %~nx0 ^<command^> [args...]
  exit /b 2
)

if defined VSCMD_VER goto :env_ready

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo error: Visual Studio not found. Install Visual Studio 2022 with "Desktop development with C++".
  exit /b 1
)

set "VSDIR="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo error: Visual Studio has no C++ tools. Add the "Desktop development with C++" workload.
  exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo error: vcvars64.bat failed
  exit /b 1
)

:env_ready
if exist "%~dp0local-env.cmd" call "%~dp0local-env.cmd"

%*
exit /b %ERRORLEVEL%
