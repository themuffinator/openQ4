@echo off
setlocal

set "llvm_rc=%ProgramFiles%\LLVM\bin\llvm-rc.exe"
if exist "%llvm_rc%" (
  "%llvm_rc%" %*
  exit /b %ERRORLEVEL%
)

if defined ProgramFiles(x86) (
  set "llvm_rc=%ProgramFiles(x86)%\LLVM\bin\llvm-rc.exe"
  if exist "%llvm_rc%" (
    "%llvm_rc%" %*
    exit /b %ERRORLEVEL%
  )
)

where llvm-rc >nul 2>nul
if %ERRORLEVEL%==0 (
  llvm-rc %*
  exit /b %ERRORLEVEL%
)

where rc >nul 2>nul
if %ERRORLEVEL%==0 (
  rc %*
  exit /b %ERRORLEVEL%
)

if defined ProgramFiles(x86) (
  for /f "delims=" %%D in ('dir /b /ad /o-n "%ProgramFiles(x86)%\Windows Kits\10\bin" 2^>nul') do (
    if exist "%ProgramFiles(x86)%\Windows Kits\10\bin\%%D\arm64\rc.exe" (
      "%ProgramFiles(x86)%\Windows Kits\10\bin\%%D\arm64\rc.exe" %*
      exit /b %ERRORLEVEL%
    )
    if exist "%ProgramFiles(x86)%\Windows Kits\10\bin\%%D\x64\rc.exe" (
      "%ProgramFiles(x86)%\Windows Kits\10\bin\%%D\x64\rc.exe" %*
      exit /b %ERRORLEVEL%
    )
    if exist "%ProgramFiles(x86)%\Windows Kits\10\bin\%%D\x86\rc.exe" (
      "%ProgramFiles(x86)%\Windows Kits\10\bin\%%D\x86\rc.exe" %*
      exit /b %ERRORLEVEL%
    )
  )
)

echo No Windows resource compiler found. Install LLVM (llvm-rc) or Windows SDK (rc.exe). 1>&2
exit /b 1
