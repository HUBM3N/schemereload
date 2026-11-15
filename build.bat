@echo off
rem Builds build\schemereload.dll with no CMake. Needs Visual Studio 2022 (or Build Tools) with the C++ workload.
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo Visual Studio not found. Install VS 2022 with "Desktop development with C++". & exit /b 1)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (echo Visual Studio C++ tools not found. Add the "Desktop development with C++" workload. & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1
if not exist build mkdir build
cl /nologo /O2 /MT /EHsc /std:c++17 /LD schemereload.cpp /Fo:build\ /Fe:build\schemereload.dll /link kernel32.lib || exit /b 1
echo Built build\schemereload.dll
