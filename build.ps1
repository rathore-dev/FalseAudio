# Builds FalseAudio with the MSVC toolchain + Ninja.
#   .\build.ps1                 -> Release build of everything
#   .\build.ps1 -Config Debug
#   .\build.ps1 -Target FalseAudioTests -Run
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")] [string] $Config = "Release",
    [string] $Target = "all",
    [switch] $Run,
    [switch] $Clean
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$buildDir = Join-Path $root "build\$Config"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "Visual Studio C++ build tools not found." }

# Import the x64 developer environment into this PowerShell session.
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
$env:PATH = "$(Split-Path $vswhere);$env:PATH"
cmd /c "`"$vcvars`" >nul 2>nul && set" | ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
}

# Tools installed by winget may not be on PATH in older shells.
foreach ($p in @("C:\Program Files\CMake\bin", "$env:LOCALAPPDATA\Microsoft\WinGet\Links")) {
    if ((Test-Path $p) -and ($env:PATH -notlike "*$p*")) { $env:PATH = "$p;$env:PATH" }
}

if ($Clean -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }

cmake -S $root -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $buildDir --target $Target
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Run) {
    $exe = Get-ChildItem -Path $buildDir -Recurse -Filter "$Target.exe" | Select-Object -First 1
    if ($exe) { & $exe.FullName; exit $LASTEXITCODE }
}
