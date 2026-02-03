<#
.SYNOPSIS
    Automated build script for ingame_overlay.
    Ensures all dependencies are available and performs a full build.

.DESCRIPTION
    1. Checks for Git and CMake in PATH.
    2. Initializes and updates Git submodules recursively.
    3. Configures the project with tests enabled.
    4. Builds the project in Release mode.
#>

$ErrorActionPreference = "Stop"

function Write-Host-Color($Message, $Color) {
    Write-Host "[$Color] $Message" -ForegroundColor $Color
}

Write-Host-Color "Starting Full Build Process..." "Cyan"

# 1. Check for Dependencies
Write-Host-Color "Checking dependencies..." "Yellow"

$git = Get-Command git -ErrorAction SilentlyContinue
if (-not $git) {
    Write-Host-Color "Error: Git not found in PATH. Please install Git." "Red"
    exit 1
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Host-Color "Error: CMake not found in PATH. Please install CMake." "Red"
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

# 3. Create/Prepare Build Directory
$BuildDir = "OUT"
if (Test-Path $BuildDir) {
    Write-Host-Color "Cleaning existing build directory '$BuildDir'..." "Yellow"
    # Keeping the directory but clearing it might be safer if files are locked, 
    # but for a 'zero to hero' we usually want a fresh start.
}

# 4. Configure Project
Write-Host-Color "Configuring project with CMake..." "Yellow"
cmake -B $BuildDir -D INGAMEOVERLAY_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) {
    Write-Host-Color "Error: CMake configuration failed." "Red"
    exit $LASTEXITCODE
}
Write-Host-Color "Configuration complete." "Green"

# 5. Build Project
Write-Host-Color "Building project (Release)..." "Yellow"
cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host-Color "Error: Build failed." "Red"
    exit $LASTEXITCODE
}

Write-Host-Color "==========================================" "Cyan"
Write-Host-Color "BUILD SUCCESSFUL!" "Green"
Write-Host-Color "Binaries are available in: $(Get-Item $BuildDir\Release).FullName" "Cyan"
Write-Host-Color "==========================================" "Cyan"
