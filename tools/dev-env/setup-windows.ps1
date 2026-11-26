<#
.SYNOPSIS
  One-time Windows developer setup for BrazenWorks.

.DESCRIPTION
  Installs what the build needs, then writes CMakeUserPresets.json so that
  `cmake --preset dev` works:

    * Git, CMake, Ninja, Python, Rust (rustup)   via winget, if missing
    * Open CASCADE                               via vcpkg (compiled once, ~30-60 min)
    * Qt 6                                       via aqtinstall (prebuilt, a few minutes)

  Needs Visual Studio 2022 or newer with the "Desktop development with C++"
  workload. Run from an elevated (administrator) PowerShell:

    powershell -ExecutionPolicy Bypass -File tools\dev-env\setup-windows.ps1

  Safe to re-run: every step is skipped if already done.

.PARAMETER Repo
  BrazenWorks checkout. Default: the checkout containing this script.
.PARAMETER VcpkgRoot
  Where vcpkg lives or will be cloned. Keep it short. Default: $env:VCPKG_ROOT or C:\vcpkg.
.PARAMETER QtRoot
  Where Qt is installed. Default: C:\Qt.
.PARAMETER QtVersion
  Qt version to install. Default: 6.8.3.
.PARAMETER CI
  Non-interactive mode for GitHub Actions: no admin check, no winget.
#>
[CmdletBinding()]
param(
  [string]$Repo = '',
  [string]$VcpkgRoot = $(if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { 'C:\vcpkg' }),
  [string]$QtRoot = 'C:\Qt',
  [string]$QtVersion = '6.8.3',
  [switch]$CI
)

$ErrorActionPreference = 'Stop'

# Windows PowerShell 5.1 leaves $PSScriptRoot empty while evaluating param()
# defaults, so resolve paths here, in the script body.
$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $Repo) { $Repo = (Resolve-Path (Join-Path $ScriptDir '..\..')).Path }
$Triplet = 'x64-windows'
$QtArch = 'win64_msvc2022_64'
$QtDir = Join-Path $QtRoot "$QtVersion\msvc2022_64"

function Write-Step([string]$Message) { Write-Host "`n==> $Message" -ForegroundColor Cyan }
function Write-Ok([string]$Message) { Write-Host "    $Message" -ForegroundColor Green }

function Update-SessionPath {
  $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
  $user = [Environment]::GetEnvironmentVariable('Path', 'User')
  $cargo = Join-Path $env:USERPROFILE '.cargo\bin'
  $env:Path = "$machine;$user;$cargo"
}

function Test-Command([string]$Name) {
  # Ignore the Microsoft Store "python" stub, which only opens the Store.
  $cmd = Get-Command $Name -ErrorAction SilentlyContinue | Where-Object { $_.Source -notlike '*WindowsApps*' }
  return [bool]$cmd
}

# Runs a native command and stops on failure. Deliberately a simple function
# using $args, so arguments like -disableMetrics or --id pass straight through.
function Invoke-Checked {
  $file = $args[0]
  $rest = @($args | Select-Object -Skip 1)
  & $file @rest
  if ($LASTEXITCODE -ne 0) { throw "'$file $($rest -join ' ')' failed with exit code $LASTEXITCODE" }
}

function Write-Utf8NoBom([string]$Path, [string]$Text) {
  # Windows PowerShell 5.1's "-Encoding utf8" adds a BOM, which JSON readers may reject.
  [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding $false))
}

function Install-Tool([string]$Command, [string]$WingetId) {
  if (Test-Command $Command) { Write-Ok "$Command found"; return }
  if ($CI -and $Command -in @('cmake', 'ninja')) {
    Write-Ok "$Command not on PATH; Visual Studio's bundled copy will be used via msvc.cmd"
    return
  }
  if ($CI) { throw "$Command is not installed (CI mode does not use winget)" }
  if (-not (Test-Command 'winget')) {
    throw "$Command is missing and winget is unavailable. Install $WingetId manually, then re-run."
  }
  Write-Host "    installing $WingetId ..."
  Invoke-Checked winget install --id $WingetId --exact --silent --accept-source-agreements --accept-package-agreements
  Update-SessionPath
  if (-not (Test-Command $Command)) { throw "$Command still not found after installing $WingetId. Open a new terminal and re-run." }
  Write-Ok "$Command installed"
}

# ---------------------------------------------------------------------------
Write-Host "BrazenWorks Windows setup" -ForegroundColor White
Write-Host "  repo:  $Repo`n  vcpkg: $VcpkgRoot`n  Qt:    $QtDir"

if (-not $CI) {
  $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
             ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
  if (-not $isAdmin) { throw 'Run this script from an administrator PowerShell (winget installs need it).' }
}

Write-Step 'Visual Studio C++ tools'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio not found. Install Visual Studio 2022 with "Desktop development with C++".' }
$vsPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio has no C++ tools. Add the "Desktop development with C++" workload in the Visual Studio Installer.' }
Write-Ok $vsPath

Write-Step 'Command-line tools'
Update-SessionPath
Install-Tool git    'Git.Git'
Install-Tool cmake  'Kitware.CMake'
Install-Tool ninja  'Ninja-build.Ninja'
# Python: prefer the "py" launcher (python.org installs it even when python.exe
# is not on PATH).
if (-not ((Test-Command 'py') -or (Test-Command 'python'))) { Install-Tool py 'Python.Python.3.12' }
$Python = if (Test-Command 'py') { @('py', '-3') } else { @('python') }
Write-Ok "python: $($Python -join ' ')"
Install-Tool rustup 'Rustlang.Rustup'

Write-Step 'Rust toolchain'
Invoke-Checked rustup toolchain install stable-x86_64-pc-windows-msvc --profile minimal --component rustfmt --component clippy
Invoke-Checked rustup default stable-x86_64-pc-windows-msvc
Write-Ok (& rustc --version)

Write-Step "vcpkg + Open CASCADE ($Triplet)"
if (-not (Test-Path (Join-Path $VcpkgRoot 'vcpkg.exe'))) {
  if (-not (Test-Path (Join-Path $VcpkgRoot '.git'))) {
    Invoke-Checked git clone https://github.com/microsoft/vcpkg $VcpkgRoot
  }
  Invoke-Checked (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
}
Write-Host '    building opencascade (the first time takes 30-60 minutes) ...'
Invoke-Checked (Join-Path $VcpkgRoot 'vcpkg.exe') install "opencascade:$Triplet"
Write-Ok 'opencascade ready'

Write-Step "Qt $QtVersion ($QtArch)"
if (Test-Path (Join-Path $QtDir 'bin\Qt6Core.dll')) {
  Write-Ok 'already installed'
} else {
  Invoke-Checked @Python -m pip install --upgrade --quiet aqtinstall
  # aqt writes aqtinstall.log to the current folder; keep it out of the repo.
  Push-Location $env:TEMP
  try {
    Invoke-Checked @Python -m aqt install-qt windows desktop $QtVersion $QtArch --outputdir $QtRoot
  } finally {
    Pop-Location
  }
  Write-Ok "installed to $QtDir"
}

Write-Step 'Writing CMakeUserPresets.json'
$toolchain = (Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake') -replace '\\', '/'
$qtCmake = $QtDir -replace '\\', '/'
$presets = [ordered]@{
  version = 3
  configurePresets = @(
    [ordered]@{
      name = 'dev'; displayName = 'Dev (RelWithDebInfo)'; inherits = 'base'
      cacheVariables = [ordered]@{
        CMAKE_BUILD_TYPE     = 'RelWithDebInfo'
        CMAKE_TOOLCHAIN_FILE = $toolchain
        VCPKG_TARGET_TRIPLET = $Triplet
        CMAKE_PREFIX_PATH    = $qtCmake
      }
    },
    [ordered]@{
      name = 'release'; displayName = 'Release'; inherits = 'dev'
      cacheVariables = [ordered]@{ CMAKE_BUILD_TYPE = 'Release' }
    }
  )
  buildPresets = @(
    [ordered]@{ name = 'dev'; configurePreset = 'dev' },
    [ordered]@{ name = 'release'; configurePreset = 'release' }
  )
  testPresets = @(
    [ordered]@{ name = 'dev'; configurePreset = 'dev'; output = @{ outputOnFailure = $true } },
    [ordered]@{ name = 'release'; configurePreset = 'release'; output = @{ outputOnFailure = $true } }
  )
}
$presetsPath = Join-Path $Repo 'CMakeUserPresets.json'
Write-Utf8NoBom $presetsPath ($presets | ConvertTo-Json -Depth 10)
Write-Ok $presetsPath

# PATH additions that msvc.cmd applies, so the app and tests find Qt's DLLs.
# (Open CASCADE's DLLs are copied next to the executables by vcpkg.)
$localEnv = Join-Path $ScriptDir 'local-env.cmd'
@(
  '@rem Written by setup-windows.ps1. Machine-specific; not committed.'
  "@set `"PATH=$QtDir\bin;$env:USERPROFILE\.cargo\bin;%PATH%`""
) | Set-Content -Path $localEnv -Encoding ascii
Write-Ok $localEnv

Write-Host "`nDone. Build and run from the repo folder:" -ForegroundColor Green
Write-Host '  tools\dev-env\msvc.cmd cmake --preset dev'
Write-Host '  tools\dev-env\msvc.cmd cmake --build --preset dev'
Write-Host '  tools\dev-env\msvc.cmd ctest --preset dev'
Write-Host '  tools\dev-env\msvc.cmd build\dev\brazenworks.exe'
