# Builds FalseAudio with the MSVC toolchain + Ninja.
#   .\build.ps1                 -> Release build of everything
#   .\build.ps1 -Config Debug
#   .\build.ps1 -Target FalseAudioTests -Run
param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo")] [string] $Config = "Release",
    [string] $Target = "all",
    [switch] $Run,
    [switch] $Clean,
    [string] $BuildDir = "",          # separate folder per parallel worker, e.g. build/w1
    [switch] $QaHooks,                # compile the --qa-script automation hook (dev/QA builds only)
    [switch] $KeepGoing,              # continue past failing files (ninja -k 0)
    [switch] $NoCache,                # do not use sccache even if installed
    [int] $Jobs = 0                   # parallel compile jobs; defaults to 3 for per-worker builds so many workers can share the PC
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$buildDir = if ($BuildDir) { Join-Path $root $BuildDir } else { Join-Path $root "build\$Config" }

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

# Compiler cache (shared by all workers/build dirs): sccache, if installed. Needs /Z7 debug info (set in CMakeLists).
$launcherArgs = @()
$sccache = $null
if (-not $NoCache) {
    $cmd = Get-Command sccache -ErrorAction SilentlyContinue
    if ($cmd) { $sccache = $cmd.Source }
    else {
        $pkg = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Filter "Mozilla.sccache*" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($pkg) { $sccache = (Get-ChildItem $pkg.FullName -Recurse -Filter sccache.exe | Select-Object -First 1).FullName }
    }
}
if ($sccache) {
    if (-not $env:SCCACHE_DIR) { $env:SCCACHE_DIR = Join-Path $env:LOCALAPPDATA "sccache" }
    if (-not $env:SCCACHE_CACHE_SIZE) { $env:SCCACHE_CACHE_SIZE = "5G" }
    $sc = $sccache -replace "\\", "/"
    $launcherArgs = @("-DCMAKE_C_COMPILER_LAUNCHER=$sc", "-DCMAKE_CXX_COMPILER_LAUNCHER=$sc")
    Write-Host "sccache: $sccache (cache $env:SCCACHE_DIR)"
} else {
    $launcherArgs = @("-UCMAKE_C_COMPILER_LAUNCHER", "-UCMAKE_CXX_COMPILER_LAUNCHER")
}

if ($Clean -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }

cmake -S $root -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DFA_QA_HOOKS=$(if ($QaHooks) { 'ON' } else { 'OFF' })" @launcherArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Jobs -le 0 -and $BuildDir) { $Jobs = 3 }
$ninjaArgs = @()
if ($Jobs -gt 0) { $ninjaArgs += "-j$Jobs" }
if ($KeepGoing) { $ninjaArgs += @("-k", "0") }
if ($ninjaArgs.Count -gt 0) { cmake --build $buildDir --target $Target -- @ninjaArgs } else { cmake --build $buildDir --target $Target }
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Run) {
    $exe = Get-ChildItem -Path $buildDir -Recurse -Filter "$Target.exe" | Select-Object -First 1
    if ($exe) { & $exe.FullName; exit $LASTEXITCODE }
}
