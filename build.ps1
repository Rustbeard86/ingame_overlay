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

.PARAMETER Config
    Build configuration: 'Release' (default) or 'Debug'.

.PARAMETER HookDebug
    If specified, enables detailed hook debugging with stack traces (adds -DINGAMEOVERLAY_HOOK_DEBUG=ON).
    This is useful for diagnosing crashes during hook installation.
    Output goes to the debugger output window (visible in Visual Studio or DebugView).

.PARAMETER Interactive
    If specified, shows an interactive menu to select build options.
#>

param (
    [ValidateSet("x86", "x64", "Both")]
    [String]$Arch = "Both",

    [Switch]$Clean,
    
    [ValidateSet("Release", "Debug")]
    [String]$Config = "Release",
    
    [Switch]$HookDebug,
    
    [Switch]$Interactive
)

$ErrorActionPreference = "Stop"

function Write-Host-Color($Message, $Color) {
    Write-Host "[$Color] $Message" -ForegroundColor $Color
}

function Show-BuildMenu {
    $script:menuArch = "Both"
    $script:menuConfig = "Release"
    $script:menuClean = $false
    $script:menuHookDebug = $false
    
    while ($true) {
        Clear-Host
        Write-Host "============================================" -ForegroundColor Cyan
        Write-Host "     INGAME OVERLAY BUILD CONFIGURATION     " -ForegroundColor Cyan
        Write-Host "============================================" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "Current Settings:" -ForegroundColor Yellow
        Write-Host "  Architecture:   $script:menuArch" -ForegroundColor White
        Write-Host "  Configuration:  $script:menuConfig" -ForegroundColor White
        Write-Host "  Clean Build:    $(if ($script:menuClean) { 'Yes' } else { 'No' })" -ForegroundColor White
        Write-Host "  Hook Debug:     $(if ($script:menuHookDebug) { 'Yes' } else { 'No' })" -ForegroundColor White
        Write-Host ""
        Write-Host "============================================" -ForegroundColor Cyan
        Write-Host "Options:" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "  [1] Change Architecture  (x86 / x64 / Both)" -ForegroundColor White
        Write-Host "  [2] Change Configuration (Debug / Release)" -ForegroundColor White
        Write-Host "  [3] Toggle Clean Build   (delete build dir)" -ForegroundColor White
        Write-Host "  [4] Toggle Hook Debug    (stack traces + logging)" -ForegroundColor White
        Write-Host ""
        Write-Host "  [5] Build with current settings" -ForegroundColor Green
        Write-Host "  [6] Quick: Debug x64 + Hook Debug" -ForegroundColor Magenta
        Write-Host "  [7] Quick: Release Both (production)" -ForegroundColor Magenta
        Write-Host "  [8] Quick: Clean Debug Both + Hook Debug" -ForegroundColor Magenta
        Write-Host ""
        Write-Host "  [0] Exit without building" -ForegroundColor Red
        Write-Host ""
        Write-Host "============================================" -ForegroundColor Cyan
        
        $choice = Read-Host "Enter your choice"
        
        switch ($choice) {
            "1" {
                Write-Host ""
                Write-Host "Select Architecture:" -ForegroundColor Yellow
                Write-Host "  [1] x86 only"
                Write-Host "  [2] x64 only"
                Write-Host "  [3] Both (x86 + x64)"
                $archChoice = Read-Host "Choice"
                switch ($archChoice) {
                    "1" { $script:menuArch = "x86" }
                    "2" { $script:menuArch = "x64" }
                    "3" { $script:menuArch = "Both" }
                    default { Write-Host "Invalid choice, keeping current." -ForegroundColor Red; Start-Sleep -Seconds 1 }
                }
            }
            "2" {
                Write-Host ""
                Write-Host "Select Configuration:" -ForegroundColor Yellow
                Write-Host "  [1] Debug   (symbols, no optimization, debug logging)"
                Write-Host "  [2] Release (optimized, production ready)"
                $configChoice = Read-Host "Choice"
                switch ($configChoice) {
                    "1" { $script:menuConfig = "Debug" }
                    "2" { $script:menuConfig = "Release" }
                    default { Write-Host "Invalid choice, keeping current." -ForegroundColor Red; Start-Sleep -Seconds 1 }
                }
            }
            "3" {
                $script:menuClean = -not $script:menuClean
                $state = if ($script:menuClean) { "ENABLED" } else { "DISABLED" }
                Write-Host "Clean build $state" -ForegroundColor Green
                Start-Sleep -Milliseconds 500
            }
            "4" {
                $script:menuHookDebug = -not $script:menuHookDebug
                $state = if ($script:menuHookDebug) { "ENABLED" } else { "DISABLED" }
                Write-Host "Hook debug $state" -ForegroundColor Green
                if ($script:menuHookDebug) {
                    Write-Host "  Stack traces and detailed logging will be captured." -ForegroundColor Yellow
                    Write-Host "  View output in Visual Studio or DebugView." -ForegroundColor Yellow
                }
                Start-Sleep -Seconds 1
            }
            "5" {
                return @{
                    Arch = $script:menuArch
                    Config = $script:menuConfig
                    Clean = $script:menuClean
                    HookDebug = $script:menuHookDebug
                    ShouldBuild = $true
                }
            }
            "6" {
                # Quick: Debug x64 + Hook Debug
                return @{
                    Arch = "x64"
                    Config = "Debug"
                    Clean = $false
                    HookDebug = $true
                    ShouldBuild = $true
                }
            }
            "7" {
                # Quick: Release Both (production)
                return @{
                    Arch = "Both"
                    Config = "Release"
                    Clean = $false
                    HookDebug = $false
                    ShouldBuild = $true
                }
            }
            "8" {
                # Quick: Clean Debug Both + Hook Debug
                return @{
                    Arch = "Both"
                    Config = "Debug"
                    Clean = $true
                    HookDebug = $true
                    ShouldBuild = $true
                }
            }
            "0" {
                return @{ ShouldBuild = $false }
            }
            default {
                Write-Host "Invalid choice. Please try again." -ForegroundColor Red
                Start-Sleep -Seconds 1
            }
        }
    }
}

function Build-Arch($Architecture, $BuildConfig, $EnableHookDebug) {
    $Platform = if ($Architecture -eq "x86") { "Win32" } else { "x64" }
    $BuildDir = "OUT_$Architecture"
    
    # Build CMake arguments
    $cmakeArgs = @("-B", $BuildDir, "-A", $Platform, "-D", "INGAMEOVERLAY_BUILD_TESTS=ON")
    
    if ($EnableHookDebug) {
        $cmakeArgs += @("-D", "INGAMEOVERLAY_HOOK_DEBUG=ON")
        Write-Host-Color "Hook debugging ENABLED - stack traces will be captured" "Magenta"
    }

    Write-Host-Color "--- Starting $BuildConfig Build for $Architecture ($Platform) ---" "Cyan"

    if ($script:doClean -and (Test-Path $BuildDir)) {
        Write-Host-Color "Cleaning existing build directory '$BuildDir'..." "Yellow"
        Remove-Item -Path $BuildDir -Recurse -Force
    }

    # Configure
    Write-Host-Color "Configuring $Architecture project with CMake..." "Yellow"
    Write-Host-Color "CMake args: $($cmakeArgs -join ' ')" "Gray"
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host-Color "Error: CMake configuration failed for $Architecture." "Red"
        return $LASTEXITCODE
    }

    # Build
    Write-Host-Color "Building $Architecture project ($BuildConfig)..." "Yellow"
    cmake --build $BuildDir --config $BuildConfig
    if ($LASTEXITCODE -ne 0) {
        Write-Host-Color "Error: Build failed for $Architecture." "Red"
        return $LASTEXITCODE
    }

    Write-Host-Color "Build SUCCESSFUL for $Architecture ($BuildConfig)!" "Green"
    Write-Host-Color "Binaries: $(Get-Item $BuildDir\$BuildConfig).FullName" "Cyan"
    
    if ($EnableHookDebug) {
        Write-Host-Color "NOTE: Hook debug output goes to debugger output window." "Yellow"
        Write-Host-Color "      Use Visual Studio debugger or DebugView (https://learn.microsoft.com/en-us/sysinternals/downloads/debugview) to view." "Yellow"
    }
    
    return @{
        Success = $true
        BuildDir = $BuildDir
        Config = $BuildConfig
        Arch = $Architecture
        HookDebug = $EnableHookDebug
    }
}

function Copy-BuildOutputs {
    param (
        [array]$BuildResults,
        [string]$Config,
        [bool]$HookDebug
    )
    
    # Determine output folder name
    $outputName = $Config.ToLower()
    if ($HookDebug) {
        $outputName += "_hookdebug"
    }
    
    $destBase = "BUILD\$outputName"
    
    Write-Host-Color "--- Copying build outputs to $destBase ---" "Cyan"
    
    # Create destination directory
    if (-not (Test-Path $destBase)) {
        New-Item -ItemType Directory -Path $destBase -Force | Out-Null
    }
    
    $copiedFiles = @()
    
    foreach ($result in $BuildResults) {
        if (-not $result.Success) { continue }
        
        $sourceDir = "$($result.BuildDir)\$($result.Config)"
        $arch = $result.Arch
        
        # Create arch subdirectory
        $archDest = "$destBase\$arch"
        if (-not (Test-Path $archDest)) {
            New-Item -ItemType Directory -Path $archDest -Force | Out-Null
        }
        
        # Copy .lib files
        $libFiles = Get-ChildItem -Path $sourceDir -Filter "*.lib" -ErrorAction SilentlyContinue
        foreach ($lib in $libFiles) {
            Copy-Item -Path $lib.FullName -Destination $archDest -Force
            $copiedFiles += "$archDest\$($lib.Name)"
        }
        
        # Copy .dll files
        $dllFiles = Get-ChildItem -Path $sourceDir -Filter "*.dll" -ErrorAction SilentlyContinue
        foreach ($dll in $dllFiles) {
            Copy-Item -Path $dll.FullName -Destination $archDest -Force
            $copiedFiles += "$archDest\$($dll.Name)"
        }
        
        # Copy .pdb files (for debug builds)
        if ($Config -eq "Debug") {
            $pdbFiles = Get-ChildItem -Path $sourceDir -Filter "*.pdb" -ErrorAction SilentlyContinue
            foreach ($pdb in $pdbFiles) {
                Copy-Item -Path $pdb.FullName -Destination $archDest -Force
                $copiedFiles += "$archDest\$($pdb.Name)"
            }
        }
        
        Write-Host-Color "  Copied $arch binaries to $archDest" "Green"
    }
    
    # Summary
    Write-Host ""
    Write-Host-Color "Build outputs copied to:" "Cyan"
    Write-Host-Color "  $((Get-Item $destBase).FullName)" "Yellow"
    Write-Host ""
    Write-Host-Color "Files:" "White"
    foreach ($file in $copiedFiles) {
        Write-Host-Color "  - $file" "Gray"
    }
    
    return $destBase
}

# Handle interactive mode
if ($Interactive -or ($args.Count -eq 0 -and -not $PSBoundParameters.ContainsKey('Arch') -and -not $PSBoundParameters.ContainsKey('Config'))) {
    # Auto-detect: if no arguments provided at all, show menu
    $menuResult = Show-BuildMenu
    
    if (-not $menuResult.ShouldBuild) {
        Write-Host "Build cancelled." -ForegroundColor Yellow
        exit 0
    }
    
    $Arch = $menuResult.Arch
    $Config = $menuResult.Config
    $script:doClean = $menuResult.Clean
    $HookDebug = $menuResult.HookDebug
    
    Write-Host ""
} else {
    $script:doClean = $Clean
}

# Show configuration summary before building
function Show-BuildSummary {
    param (
        [string]$BuildArch,
        [string]$BuildConfig,
        [bool]$BuildClean,
        [bool]$BuildHookDebug
    )
    
    Write-Host ""
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host "          BUILD CONFIGURATION SUMMARY       " -ForegroundColor Cyan
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host ""
    
    # Architecture
    $archDisplay = switch ($BuildArch) {
        "x86"  { "x86 (32-bit)" }
        "x64"  { "x64 (64-bit)" }
        "Both" { "Both (x86 + x64)" }
    }
    Write-Host "  Architecture:    " -NoNewline -ForegroundColor White
    Write-Host $archDisplay -ForegroundColor Yellow
    
    # Configuration
    $configColor = if ($BuildConfig -eq "Debug") { "Magenta" } else { "Green" }
    Write-Host "  Configuration:   " -NoNewline -ForegroundColor White
    Write-Host $BuildConfig -ForegroundColor $configColor
    
    # Clean
    Write-Host "  Clean Build:     " -NoNewline -ForegroundColor White
    if ($BuildClean) {
        Write-Host "Yes (deleting build directories)" -ForegroundColor Yellow
    } else {
        Write-Host "No (incremental)" -ForegroundColor Gray
    }
    
    # Hook Debug
    Write-Host "  Hook Debug:      " -NoNewline -ForegroundColor White
    if ($BuildHookDebug) {
        Write-Host "ENABLED (stack traces + logging)" -ForegroundColor Magenta
    } else {
        Write-Host "Disabled" -ForegroundColor Gray
    }
    
    # Output directories
    Write-Host ""
    Write-Host "  Output:" -ForegroundColor White
    if ($BuildArch -eq "Both" -or $BuildArch -eq "x64") {
        Write-Host "    - OUT_x64\$BuildConfig\" -ForegroundColor Gray
    }
    if ($BuildArch -eq "Both" -or $BuildArch -eq "x86") {
        Write-Host "    - OUT_x86\$BuildConfig\" -ForegroundColor Gray
    }
    
    # Show final output directory
    $outputName = $BuildConfig.ToLower()
    if ($BuildHookDebug) {
        $outputName += "_hookdebug"
    }
    Write-Host ""
    Write-Host "  Final Output:    " -NoNewline -ForegroundColor White
    Write-Host "BUILD\$outputName\" -ForegroundColor Green
    
    Write-Host ""
    Write-Host "============================================" -ForegroundColor Cyan
    Write-Host ""
}

Show-BuildSummary -BuildArch $Arch -BuildConfig $Config -BuildClean $script:doClean -BuildHookDebug $HookDebug

# Confirm before building (only in interactive mode)
if ($Interactive -or ($args.Count -eq 0 -and -not $PSBoundParameters.ContainsKey('Arch') -and -not $PSBoundParameters.ContainsKey('Config'))) {
    $confirm = Read-Host "Proceed with build? [Y/n]"
    if ($confirm -eq 'n' -or $confirm -eq 'N') {
        Write-Host "Build cancelled." -ForegroundColor Yellow
        exit 0
    }
    Write-Host ""
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
$buildResults = @()

foreach ($a in $archsToBuild) {
    $res = Build-Arch $a $Config $HookDebug
    if (-not $res.Success) {
        exit 1
    }
    $buildResults += $res
}

# 4. Copy outputs to BUILD directory
$outputDir = Copy-BuildOutputs -BuildResults $buildResults -Config $Config -HookDebug $HookDebug

Write-Host ""
Write-Host-Color "==========================================" "Cyan"
Write-Host-Color "ALL REQUESTED BUILDS SUCCESSFUL!" "Green"
Write-Host-Color "==========================================" "Cyan"
Write-Host-Color "Output directory: $outputDir" "Yellow"
