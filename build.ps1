<#
    Vigil build script (Windows).

    Prefers the MSVC toolchain (cl.exe + rc.exe) if a Visual Studio
    "Developer" environment is active; otherwise falls back to MinGW-w64
    (g++ + windres) if it is on PATH.

    Usage:
        pwsh ./build.ps1              # build Release x64 -> build/Vigil.exe
        pwsh ./build.ps1 -Run         # build, then launch it

    Output: build/Vigil.exe  (static, no runtime dependencies, ~140 KB)
#>
param([switch]$Run)

$ErrorActionPreference = "Stop"
$root  = $PSScriptRoot
$src   = Join-Path $root "src"
$build = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $build | Out-Null

function Have($name) { $null -ne (Get-Command $name -ErrorAction SilentlyContinue) }

if ((Have "cl") -and (Have "rc")) {
    Write-Host "Building with MSVC..." -ForegroundColor Cyan
    Push-Location $build
    try {
        & rc /nologo /fo "vigil_res.res" (Join-Path $src "vigil.rc")
        if ($LASTEXITCODE) { throw "rc failed" }
        & cl /nologo /O2 /EHsc /DUNICODE /D_UNICODE /W3 `
            (Join-Path $src "main.cpp") "vigil_res.res" `
            /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup `
            user32.lib shell32.lib advapi32.lib ole32.lib `
            /OUT:"Vigil.exe"
        if ($LASTEXITCODE) { throw "cl failed" }
    } finally { Pop-Location }
}
elseif ((Have "g++") -and (Have "windres")) {
    Write-Host "Building with MinGW-w64..." -ForegroundColor Cyan
    Push-Location $src
    try { & windres "vigil.rc" -O coff -o (Join-Path $build "vigil_res.o") }
    finally { Pop-Location }
    if ($LASTEXITCODE) { throw "windres failed" }
    & g++ -municode -O2 -s -mwindows -static -static-libgcc -static-libstdc++ `
        -o (Join-Path $build "Vigil.exe") `
        (Join-Path $src "main.cpp") (Join-Path $build "vigil_res.o") `
        -lshell32 -lole32 -ladvapi32 -luser32
    if ($LASTEXITCODE) { throw "g++ failed" }
}
else {
    throw "No compiler found. Install Visual Studio Build Tools (cl/rc) or MinGW-w64 (g++/windres)."
}

$exe = Join-Path $build "Vigil.exe"
Write-Host "Built: $exe" -ForegroundColor Green
Get-Item $exe | Select-Object Name, Length, LastWriteTime | Format-Table | Out-String | Write-Host

if ($Run) { Start-Process $exe }
