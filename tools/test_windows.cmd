@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "weather_vs_path=%%i"
if not defined weather_vs_path (
  echo Visual Studio C++ build tools are required.
  exit /b 1
)
call "%weather_vs_path%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64
if errorlevel 1 exit /b 1
pushd "%~dp0.."
python tools\run_tests.py
set "weather_test_result=%errorlevel%"
popd
exit /b %weather_test_result%
