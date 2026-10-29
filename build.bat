@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
  echo [build] vcvars64.bat failed
  exit /b 1
)
cl /nologo /EHsc /std:c++20 /W3 /DUNICODE /D_UNICODE /utf-8 %*
exit /b %errorlevel%
