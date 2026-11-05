@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /std:c11 /I firmware tests\test_wind_model.c firmware\wind_model.c /Febuild\test_wind_model.exe /Fobuild\
if errorlevel 1 exit /b 1
build\test_wind_model.exe
