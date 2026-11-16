# Packages FalseAudio: portable zip + (if Inno Setup present) installer.
#   .\package.ps1              build Release into build\p1, stage, zip, smoke test
#   .\package.ps1 -SkipBuild   reuse existing build
param(
    [switch] $SkipBuild,
    [string] $BuildDir = "build\lead",
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

# Self-contained exe: static CRT + gear database embedded, so nothing else is staged.

@"
FalseAudio $version - tester build (Windows 64-bit)

INSTALL
  1. Right-click the zip -> Extract All... (do NOT run from inside the zip),
     or just use the single FalseAudio-$version-win64.exe.
  2. Run FalseAudio.exe.
  3. Windows SmartScreen may warn because the app is unsigned:
     click "More info" -> "Run anyway".
  FalseAudio.exe is fully self-contained (no DLLs or data folder needed).
  Optional: a data\gear folder next to the exe overrides the built-in equipment database.

WHAT TO TEST
  - App starts, all pages open, Demo/Live switch works.
  - Equipment page: pick products, add your own, apply driver specs.
  - Measure / Live pages with your audio interface and mics.
  - Crossovers & EQ, tuning score, anything that looks wrong or crashes.

IF IT CRASHES
  Send the file  %APPDATA%\FalseAudio\crash.log
  (paste that path into Explorer's address bar), plus what you were doing.
"@ | Set-Content (Join-Path $stage "README.txt") -Encoding UTF8

$single = Join-Path $dist "FalseAudio-$version-win64.exe"
Copy-Item $exe $single -Force
Write-Host ("Single exe: {0} ({1:N1} MB)" -f $single, ((Get-Item $single).Length / 1MB))

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

# Smoke test: single exe alone in an empty temp folder, 8 s alive + Equipment DB loaded (QA snapshot)
$tmp = Join-Path $env:TEMP ("FalseAudio_smoke_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $tmp | Out-Null
$test = Join-Path $tmp "FalseAudio.exe"
Copy-Item $single $test
$snap = Join-Path $dist "smoke-gear.png"
if (Test-Path $snap) { Remove-Item $snap -Force }
$qa = Join-Path $tmp "qa.txt"
"page gear`nwait 3000`nsnap $snap`nquit" | Set-Content $qa -Encoding ASCII
$p = Start-Process $test -ArgumentList @("--qa-script", "`"$qa`"") -WorkingDirectory $tmp -PassThru
Start-Sleep -Seconds 8
$alive = -not $p.HasExited
# startup (audio device scan) can take 10-20 s on a loaded PC: wait up to 90 s for the snapshot
$deadline = (Get-Date).AddSeconds(90)
while ((-not (Test-Path $snap)) -and (-not $p.HasExited) -and ((Get-Date) -lt $deadline)) { Start-Sleep -Milliseconds 500 }
Start-Sleep -Seconds 1
if (-not $p.HasExited) { Stop-Process $p -Force }
Start-Sleep -Milliseconds 500
Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
if (-not $alive) { Write-Host "SMOKE TEST FAILED: exited with code $($p.ExitCode)"; exit 1 }
if (-not (Test-Path $snap)) { Write-Host "SMOKE TEST FAILED: gear page snapshot not produced"; exit 1 }
Write-Host "SMOKE TEST PASSED: single exe stayed alive 8 s; gear page snapshot: $snap (inspect it to confirm products are listed)"
