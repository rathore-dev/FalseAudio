# Packages FalseAudio: portable zip + (if Inno Setup present) installer.
#   .\package.ps1              build Release into build\p1, stage, zip, smoke test
#   .\package.ps1 -SkipBuild   reuse existing build
param(
    [switch] $SkipBuild,
    [string] $BuildDir = "build\p1",
    [int] $Jobs = 3
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
Set-Location $root

$cm = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cm -notmatch 'project\(FalseAudio\s+VERSION\s+([\d\.]+)') { throw "Cannot read version from CMakeLists.txt" }
$version = $Matches[1]
Write-Host "FalseAudio version $version"

if (-not $SkipBuild) {
    & (Join-Path $root "build.ps1") -Target FalseAudio -BuildDir $BuildDir -Jobs $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}
$exe = Join-Path $root "$BuildDir\FalseAudio_artefacts\Release\FalseAudio.exe"
if (-not (Test-Path $exe)) { throw "Exe not found: $exe" }

$dist  = Join-Path $root "dist"
$stage = Join-Path $dist "FalseAudio"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage

# MSVC runtime DLLs (newest redist version found)
$redistRoots = @("${env:ProgramFiles(x86)}\Microsoft Visual Studio", "$env:ProgramFiles\Microsoft Visual Studio")
$crt = $null
foreach ($r in $redistRoots) {
    if (-not (Test-Path $r)) { continue }
    $crt = Get-ChildItem $r -Recurse -Directory -Filter "Microsoft.VC143.CRT" -ErrorAction SilentlyContinue |
           Where-Object { $_.FullName -like '*\x64\*' -and $_.FullName -like '*Redist\MSVC*' } |
           Sort-Object FullName -Descending | Select-Object -First 1
    if ($crt) { break }
}
if (-not $crt) { throw "MSVC redist CRT folder not found" }
Write-Host "Runtime DLLs from $($crt.FullName)"
Copy-Item (Join-Path $crt.FullName "*.dll") $stage

Copy-Item (Join-Path $root "data\gear") (Join-Path $stage "data\gear") -Recurse

@"
FalseAudio $version - tester build (Windows 64-bit)

INSTALL
  1. Right-click the zip -> Extract All... (do NOT run from inside the zip).
  2. Open the extracted FalseAudio folder and run FalseAudio.exe.
  3. Windows SmartScreen may warn because the app is unsigned:
     click "More info" -> "Run anyway".
  Keep the data\ folder and the .dll files next to FalseAudio.exe.

WHAT TO TEST
  - App starts, all pages open, Demo/Live switch works.
  - Equipment page: pick products, add your own, apply driver specs.
  - Measure / Live pages with your audio interface and mics.
  - Crossovers & EQ, tuning score, anything that looks wrong or crashes.

IF IT CRASHES
  Send the file  %APPDATA%\FalseAudio\crash.log
  (paste that path into Explorer's address bar), plus what you were doing.
"@ | Set-Content (Join-Path $stage "README.txt") -Encoding UTF8

$zip = Join-Path $dist "FalseAudio-$version-win64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
tar -a -c -f $zip -C $dist FalseAudio
if ($LASTEXITCODE -ne 0) { throw "tar failed" }
Write-Host ("Zip: {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))

# Installer
$iscc = $null
foreach ($p in @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe")) {
    if (Test-Path $p) { $iscc = $p; break }
}
if (-not $iscc) { $c = Get-Command ISCC.exe -ErrorAction SilentlyContinue; if ($c) { $iscc = $c.Source } }
if ($iscc) {
    & $iscc "/DAppVersion=$version" (Join-Path $root "installer\FalseAudio.iss")
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }
    Write-Host "Installer: dist\FalseAudio-$version-Setup.exe"
} else {
    Write-Host "NOTE: Inno Setup 6 is not installed - installer skipped. Install it and re-run package.ps1 to get dist\FalseAudio-$version-Setup.exe."
}

# Smoke test: extract zip to temp, launch, confirm alive ~8 s
$tmp = Join-Path $env:TEMP ("FalseAudio_smoke_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $tmp | Out-Null
tar -x -f $zip -C $tmp
$p = Start-Process (Join-Path $tmp "FalseAudio\FalseAudio.exe") -PassThru
Start-Sleep -Seconds 8
$alive = -not $p.HasExited
if ($alive) { Stop-Process $p -Force }
Start-Sleep -Milliseconds 500
Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
if ($alive) { Write-Host "SMOKE TEST PASSED: stayed alive 8 s" } else { Write-Host "SMOKE TEST FAILED: exited with code $($p.ExitCode)"; exit 1 }
