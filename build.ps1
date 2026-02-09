<#
.SYNOPSIS
    Automated build script for ingame_overlay.
    Ensures all dependencies are available and performs a full build for x86 and/or x64.

.DESCRIPTION
    1. Checks for Git and CMake in PATH.
    2. Initializes and updates Git submodules recursively.
    3. Configures and builds for specified architectures (default: both).
    4. Uses separate output directories (OUT_x86, OUT_x64).

.PARAMETER Arch
    The architecture to build: 'x86', 'x64', or 'Both' (default).

.PARAMETER Clean
    If specified, cleans the build directories before building.
#>

param (
    [ValidateSet("x86", "x64", "Both")]
    [String]$Arch = "Both",

    [Switch]$Clean
)

$ErrorActionPreference = "Stop"

function Write-Host-Color($Message, $Color) {
    Write-Host "[$Color] $Message" -ForegroundColor $Color
}

function Build-Arch($Architecture) {
    $Platform = if ($Architecture -eq "x86") { "Win32" } else { "x64" }
    $BuildDir = "OUT_$Architecture"

    Write-Host-Color "--- Starting Build for $Architecture ($Platform) ---" "Cyan"

    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Host-Color "Cleaning existing build directory '$BuildDir'..." "Yellow"
        Remove-Item -Path $BuildDir -Recurse -Force
    }

    # Configure
    Write-Host-Color "Configuring $Architecture project with CMake..." "Yellow"
    cmake -B $BuildDir -A $Platform -D INGAMEOVERLAY_BUILD_TESTS=ON
    if ($LASTEXITCODE -ne 0) {
        Write-Host-Color "Error: CMake configuration failed for $Architecture." "Red"
        return $LASTEXITCODE
    }

    # Build
    Write-Host-Color "Building $Architecture project (Release)..." "Yellow"
    cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) {
        Write-Host-Color "Error: Build failed for $Architecture." "Red"
        return $LASTEXITCODE
    }

    Write-Host-Color "Build SUCCESSFUL for $Architecture!" "Green"
    Write-Host-Color "Binaries: $(Get-Item $BuildDir\Release).FullName" "Cyan"
    return 0
}

Write-Host-Color "Starting Multi-Architecture Build Process..." "Cyan"

# 1. Check for Dependencies
Write-Host-Color "Checking dependencies..." "Yellow"

$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) {
    Write-Host-Color "Error: Git not found in PATH." "Red"
    exit 1
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Host-Color "Error: CMake not found in PATH." "Red"
    exit 1
}

Write-Host-Color "Dependencies found: Git and CMake." "Green"

# 2. Initialize Submodules
Write-Host-Color "Synchronizing dependencies (Git submodules)..." "Yellow"
git submodule update --init --recursive
if ($LASTEXITCODE -ne 0) {
    Write-Host-Color "Error: Failed to synchronize submodules." "Red"
    exit $LASTEXITCODE
}
Write-Host-Color "Dependencies synchronized." "Green"

# 3. Build architectures
$archsToBuild = if ($Arch -eq "Both") { @("x64", "x86") } else { @($Arch) }

foreach ($a in $archsToBuild) {
    $res = Build-Arch $a
    if ($res -ne 0) {
        exit $res
    }
}

Write-Host-Color "==========================================" "Cyan"
Write-Host-Color "ALL REQUESTED BUILDS SUCCESSFUL!" "Green"
Write-Host-Color "==========================================" "Cyan"
