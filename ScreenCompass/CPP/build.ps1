$ErrorActionPreference = "Continue"

# Find Visual Studio
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath

if (-not $vsPath) {
    Write-Error "Visual Studio not found"
    exit 1
}

Write-Host "Using VS at: $vsPath"

# Find vcvarsall
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"

# Build using Developer Command Prompt
$buildScript = @"
@echo off
call "$vcvars" >nul 2>&1
cd /d "$PSScriptRoot"
echo Compiling resources...
rc /nologo resource.rc
if errorlevel 1 (
    echo RC failed
    exit /b 1
)
echo Compiling main.cpp...
cl /nologo /EHsc /std:c++20 /O2 /DUNICODE /D_UNICODE main.cpp resource.res /link /OUT:ScreenCompass.exe user32.lib shell32.lib gdi32.lib ole32.lib windowsapp.lib
if errorlevel 1 (
    echo CL failed
    exit /b 1
)
del /q *.obj *.res 2>nul
echo Build complete
"@

$tempBat = Join-Path $env:TEMP "build_oriented.bat"
$buildScript | Out-File -FilePath $tempBat -Encoding ASCII

& cmd /c $tempBat 2>&1

if ($LASTEXITCODE -eq 0 -and (Test-Path "$PSScriptRoot\ScreenCompass.exe")) {
    $size = (Get-Item "$PSScriptRoot\ScreenCompass.exe").Length
    Write-Host "`nBuild succeeded: ScreenCompass.exe ($size bytes)"
} else {
    Write-Host "Build failed with exit code $LASTEXITCODE"
}
