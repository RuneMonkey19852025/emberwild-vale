#Requires -Version 5.1
<#
.SYNOPSIS
    Emberwild Vale - one-shot Windows developer environment setup.

.DESCRIPTION
    Installs everything required to compile and run this Unreal Engine 5.8 C++
    project on Windows, then configures and builds it:

      1. Git + Git LFS   - the repo stores .pak/.ucas/.utoc/.ushaderbytecode
                           through LFS, so a clone without LFS is incomplete.
      2. Visual Studio   - with the workloads/components Epic requires for 5.8.
      3. Windows SDK     - the 10.0.26100 SDK (Epic's recommended version).
      4. Epic Games Launcher - installs the UE 5.8 engine.
      5. Git LFS pull    - materialises the real binary content.
      6. Generate project files + build the EmberwildValeEditor target.

    Unreal Engine 5.8 itself must be installed through the Epic Games Launcher
    (it needs your Epic account and ~75 GB); this script installs the launcher
    and verifies the engine, but cannot silently fetch the engine for you.

.PARAMETER VisualStudioVersion
    Visual Studio major channel to install: 2026 (default; Epic recommends it
    for UE 5.8) or 2022 (17.14 or later, also supported).

.PARAMETER ProjectPath
    Project root containing EmberwildVale.uproject. Defaults to this script's dir.

.PARAMETER EngineRoot
    UE 5.8 root, e.g. "C:\Program Files\Epic Games\UE_5.8". Auto-detected if omitted.

.PARAMETER SkipVisualStudio
    Do not install or modify Visual Studio.

.PARAMETER SkipBuild
    Install/verify tooling only; skip project generation and compilation.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\setup-windows.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\setup-windows.ps1 -VisualStudioVersion 2022
#>
[CmdletBinding()]
param(
    [ValidateSet('2026', '2022')]
    [string]$VisualStudioVersion = '2026',

    [string]$ProjectPath = $PSScriptRoot,

    [string]$EngineRoot = '',

    [switch]$SkipVisualStudio,

    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Write-Step { param([string]$Message) Write-Host "`n==> $Message" -ForegroundColor Cyan }
function Write-Ok { param([string]$Message) Write-Host "    $Message" -ForegroundColor Green }
function Write-Note { param([string]$Message) Write-Host "    $Message" -ForegroundColor Yellow }

function Test-CommandExists {
    param([string]$Name)
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Install-WingetPackage {
    param(
        [Parameter(Mandatory)][string]$Id,
        [string]$OverrideArgs = ''
    )
    Write-Step "Installing $Id"
    $wingetArgs = @(
        'install', '--id', $Id, '--exact',
        '--accept-package-agreements', '--accept-source-agreements',
        '--disable-interactivity'
    )
    if ($OverrideArgs) { $wingetArgs += @('--override', $OverrideArgs) }
    & winget @wingetArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Note "winget exited with code $LASTEXITCODE for $Id (already installed is fine)."
    }
}

function Find-EngineRoot {
    # 1. The registry value the Epic Games Launcher writes.
    foreach ($key in @(
            'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8',
            'HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\5.8')) {
        try {
            $dir = (Get-ItemProperty -Path $key -Name InstalledDirectory -ErrorAction Stop).InstalledDirectory
            if ($dir -and (Test-Path (Join-Path $dir 'Engine\Build\BatchFiles\Build.bat'))) { return $dir }
        } catch { }
    }
    # 2. Default launcher install locations, newest "UE_5.*" first.
    $bases = @("$env:ProgramFiles\Epic Games", "${env:ProgramFiles(x86)}\Epic Games")
    foreach ($base in $bases) {
        if (-not (Test-Path $base)) { continue }
        $hit = Get-ChildItem $base -Directory -ErrorAction SilentlyContinue |
            Where-Object { Test-Path (Join-Path $_.FullName 'Engine\Build\BatchFiles\Build.bat') } |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}

# ---------------------------------------------------------------------------
Write-Step 'Checking host prerequisites'

if ($env:OS -ne 'Windows_NT' -and -not $IsWindows) {
    Write-Note 'This script targets Windows.'
    exit 1
}

$isAdmin = ([Security.Principal.WindowsPrincipal] `
        [Security.Principal.WindowsIdentity]::GetCurrent()
).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Note 'Not elevated - Windows will show UAC prompts during installs.'
    Write-Note 'If a step fails, re-run this script from an Administrator PowerShell.'
}
Write-Ok "PowerShell $($PSVersionTable.PSVersion)"

if (-not (Test-CommandExists 'winget')) {
    Write-Note 'winget (App Installer) was not found.'
    Write-Note 'Install "App Installer" from the Microsoft Store, then re-run this script.'
    exit 1
}
Write-Ok 'winget available'

# ---------------------------------------------------------------------------
Write-Step 'Installing Git and Git LFS'
if (Test-CommandExists 'git') { Write-Ok "git already present: $((git --version))" }
else { Install-WingetPackage -Id 'Git.Git' }

if (Test-CommandExists 'git-lfs') { Write-Ok 'git-lfs already present' }
else { Install-WingetPackage -Id 'GitHub.GitLFS' }

# Refresh PATH for the current session so newly installed tools are visible.
$machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$env:Path = (@($machinePath, $userPath) | Where-Object { $_ }) -join ';'

# winget writes PATH to the registry, which the running shell only reads at
# startup, and Git LFS is mandatory for this repo - make sure the default
# install locations are usable immediately.
foreach ($dir in @("$env:ProgramFiles\Git\cmd", "$env:ProgramFiles\Git LFS")) {
    if ((Test-Path $dir) -and (($env:Path -split ';') -notcontains $dir)) {
        $env:Path = "$env:Path;$dir"
    }
}

# ---------------------------------------------------------------------------
if (-not $SkipVisualStudio) {
    Write-Step "Installing Visual Studio $VisualStudioVersion with the UE 5.8 workloads"

    # Workload + component IDs from Epic's "Setting Up Visual Studio" (UE 5.8)
    # and Microsoft's Visual Studio workload/component ID reference.
    $components = @(
        'Microsoft.VisualStudio.Workload.NativeDesktop',            # Desktop development with C++
        'Microsoft.VisualStudio.Workload.NativeGame',               # Game development with C++
        'Microsoft.VisualStudio.Workload.ManagedDesktop',           # .NET desktop development
        'Microsoft.VisualStudio.Workload.NetCrossPlat',             # .NET MAUI development
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',        # MSVC v143 toolset
        'Microsoft.VisualStudio.Component.VC.DiagnosticTools',      # C++ profiling tools
        'Microsoft.VisualStudio.Component.VC.ASAN',                 # C++ AddressSanitizer
        'Microsoft.VisualStudio.Component.Windows11SDK.26100'       # Windows 11 SDK 10.0.26100
    )
    # NOTE: the "Unreal Engine installer" VS component is only documented under a
    # bare 'Component.Unreal' alias, and an unknown --add id aborts the whole VS
    # install. It merely installs the Epic Games Launcher, which this script
    # installs separately via winget, so it is intentionally omitted here.
    $vsOverride = (@('--quiet', '--wait', '--norestart', '--nocache') +
        ($components | ForEach-Object { '--add'; $_ })) -join ' '

    $vsId = if ($VisualStudioVersion -eq '2026') { 'Microsoft.VisualStudio.Community' }
    else { 'Microsoft.VisualStudio.2022.Community' }

    Install-WingetPackage -Id $vsId -OverrideArgs $vsOverride
    Write-Note 'Visual Studio installs run in the background; this can take a while.'
} else {
    Write-Note 'Skipping Visual Studio (-SkipVisualStudio).'
}

# ---------------------------------------------------------------------------
Write-Step 'Installing the .NET SDK (used by UnrealBuildTool)'
Install-WingetPackage -Id 'Microsoft.DotNet.SDK.10'

# ---------------------------------------------------------------------------
Write-Step 'Installing the Epic Games Launcher'
Install-WingetPackage -Id 'EpicGames.EpicGamesLauncher'

# ---------------------------------------------------------------------------
Write-Step 'Locating Unreal Engine 5.8'
if (-not $EngineRoot) { $EngineRoot = Find-EngineRoot }

if (-not $EngineRoot) {
    Write-Note 'Unreal Engine was not found on this machine.'
    Write-Note 'Open the Epic Games Launcher, sign in, then:'
    Write-Note '    Unreal Engine -> Library -> "+" -> 5.8 -> Install  (~75 GB)'
    Write-Note 'Then re-run this script to configure LFS and build.'
    if (-not $SkipBuild) { exit 0 }
} else {
    $buildBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
    if (-not (Test-Path $buildBat)) {
        Write-Note "Engine root '$EngineRoot' has no Engine\Build\BatchFiles\Build.bat."
        exit 1
    }
    Write-Ok "Engine root: $EngineRoot"
    if ($EngineRoot -notmatch '5\.8') {
        Write-Note "This project pins EngineAssociation 5.8 but the detected engine is '$EngineRoot'."
        Write-Note 'Builds may still work; mismatches can require regenerating project files.'
    }
}

# ---------------------------------------------------------------------------
Write-Step 'Configuring Git LFS in the clone'
$uproject = Join-Path $ProjectPath 'EmberwildVale.uproject'
if (-not (Test-Path $uproject)) {
    Write-Note "EmberwildVale.uproject not found in '$ProjectPath'."
    exit 1
}

Push-Location $ProjectPath
try {
    if (Test-CommandExists 'git') {
        # git lfs install writes its status to stderr; surface it as-is.
        git lfs install
        if (Test-Path (Join-Path $ProjectPath '.git')) {
            Write-Step 'Pulling Git LFS content (pak/ucas/utoc/shader bytecode)'
            git lfs pull
            if ($LASTEXITCODE -ne 0) { Write-Note 'git lfs pull reported an error; content may be incomplete.' }
            else { Write-Ok 'LFS content present' }
        }
    } else {
        Write-Note 'git not on PATH yet - open a new terminal so Git/LFS are picked up, then re-run.'
    }
} finally {
    Pop-Location
}

if ($SkipBuild) {
    Write-Step 'Done (tooling only; -SkipBuild was set).'
    exit 0
}
if (-not (Test-CommandExists 'git') -or -not $EngineRoot) { exit 0 }

# ---------------------------------------------------------------------------
Write-Step 'Generating Visual Studio project files'
$generateBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\GenerateProjectFiles.bat'
if (Test-Path $generateBat) {
    & $generateBat "-project=$uproject" -game
    if ($LASTEXITCODE -ne 0) {
        Write-Note 'GenerateProjectFiles returned nonzero; continuing to the build anyway.'
    }
} else {
    Write-Note "GenerateProjectFiles.bat missing under '$EngineRoot'; skipping."
}

Write-Step 'Building EmberwildValeEditor (Win64 Development) - first build takes 5-15 min'
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') `
    EmberwildValeEditor Win64 Development "-project=$uproject" -WaitMutex

if ($LASTEXITCODE -ne 0) {
    Write-Host ''
    Write-Host 'Build FAILED - see the compiler output above.' -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ''
Write-Host 'Setup complete.' -ForegroundColor Green
Write-Host "  Open the editor:  $EngineRoot\Engine\Binaries\Win64\UnrealEditor.exe `"$uproject`"" -ForegroundColor Green
Write-Host "  Or double-click:  $uproject" -ForegroundColor Green
Write-Host '  Standalone game:  .\build-game.cmd game' -ForegroundColor Green
