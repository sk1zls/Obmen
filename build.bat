@echo off
rem Build script for "Obmen" (LAN shared folder) using MSVC.
rem See README.md for details (in Russian).
setlocal enabledelayedexpansion

set "VCVARS="
for %%P in (
  "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
) do (
  if not defined VCVARS if exist %%P set "VCVARS=%%~P"
)

if not defined VCVARS (
  echo [ERROR] vcvars64.bat not found. Install Visual Studio Build Tools with the C++ workload.
  exit /b 1
)

echo Using: !VCVARS!
call "!VCVARS!" >nul
if errorlevel 1 (
  echo [ERROR] Failed to initialize the MSVC environment.
  exit /b 1
)

if not exist build mkdir build

echo Resources...
rc /nologo /fo build\obmen.res res\obmen.rc
if errorlevel 1 (
  echo [ERROR] Resource compilation failed.
  exit /b 1
)

echo Compiling...
cl /nologo /std:c++17 /EHsc /O2 /utf-8 /W3 /D_CRT_SECURE_NO_WARNINGS ^
   /Ithird_party\webview2\include ^
   /Fe:build\obmen.exe /Fo:build\ ^
   src\main.cpp src\app.cpp src\net.cpp src\store.cpp src\http.cpp ^
   src\discovery.cpp src\sync.cpp src\ftp.cpp src\gui_assets.cpp src\ui.cpp ^
   build\obmen.res third_party\webview2\x64\WebView2LoaderStatic.lib ^
   ws2_32.lib shell32.lib ole32.lib oleaut32.lib user32.lib gdi32.lib ^
   advapi32.lib windowsapp.lib ^
   /link /MANIFEST:EMBED

if errorlevel 1 (
  echo [ERROR] Build failed.
  exit /b 1
)

echo.
echo Done: build\obmen.exe
echo Run:  build\obmen.exe --root "D:\Share"
