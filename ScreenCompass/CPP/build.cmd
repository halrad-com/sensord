@echo off
setlocal

:: Find Visual Studio
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSINSTALL=%%i

if not defined VSINSTALL (
    echo Visual Studio not found
    exit /b 1
)

:: Setup environment
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

:: Build
echo Building ScreenCompass...
rc /nologo resource.rc
cl /nologo /EHsc /std:c++20 /O2 /DUNICODE /D_UNICODE ^
   /I "%WindowsSdkDir%Include\%WindowsSDKVersion%\cppwinrt" ^
   main.cpp resource.res ^
   /link /OUT:ScreenCompass.exe ^
   user32.lib shell32.lib gdi32.lib ole32.lib windowsapp.lib

if %ERRORLEVEL% == 0 (
    echo.
    echo Build succeeded: ScreenCompass.exe
    for %%A in (ScreenCompass.exe) do echo Size: %%~zA bytes
) else (
    echo Build failed
)

:: Cleanup
del /q *.obj *.res 2>nul

endlocal
