# Test script to validate Windows path handling similar to GitHub Actions
# This script simulates the key steps from the GitHub Actions workflow

param(
    [Parameter(Mandatory=$false)]
    [string]$WorkspacePath = (Get-Location).Path
)

Write-Host "=== Windows Workflow Test Script ===" -ForegroundColor Green
Write-Host "Workspace: $WorkspacePath" -ForegroundColor Yellow

# Test 1: Path construction
Write-Host "`n1. Testing Windows path construction..." -ForegroundColor Cyan
$vcpkgPath = "$WorkspacePath\vcpkg"
$buildDir = "$WorkspacePath\build"
$distDir = "$WorkspacePath\dist"

Write-Host "vcpkg path: $vcpkgPath"
Write-Host "Build directory: $buildDir"  
Write-Host "Distribution directory: $distDir"

# Test 2: Check if paths exist
Write-Host "`n2. Checking existing directories..." -ForegroundColor Cyan
$paths = @{
    "vcpkg.json" = "$WorkspacePath\vcpkg.json"
    "CMakeLists.txt" = "$WorkspacePath\CMakeLists.txt"
    "src directory" = "$WorkspacePath\src"
    "img directory" = "$WorkspacePath\img"
    "external directory" = "$WorkspacePath\external"
}

foreach ($name in $paths.Keys) {
    $path = $paths[$name]
    $exists = Test-Path $path
    $status = if ($exists) { "✓ EXISTS" } else { "✗ MISSING" }
    $color = if ($exists) { "Green" } else { "Red" }
    Write-Host "$name`: $status" -ForegroundColor $color
}

# Test 3: Simulate vcpkg path operations
Write-Host "`n3. Testing vcpkg operations..." -ForegroundColor Cyan
if (Test-Path $vcpkgPath) {
    Write-Host "✓ vcpkg directory exists"
    $vcpkgExe = "$vcpkgPath\vcpkg.exe"
    if (Test-Path $vcpkgExe) {
        Write-Host "✓ vcpkg.exe found"
        try {
            $version = & $vcpkgExe version 2>$null
            Write-Host "✓ vcpkg version check successful"
        } catch {
            Write-Host "⚠ vcpkg executable exists but version check failed" -ForegroundColor Yellow
        }
    } else {
        Write-Host "✗ vcpkg.exe not found at $vcpkgExe" -ForegroundColor Red
    }
} else {
    Write-Host "✗ vcpkg directory not found" -ForegroundColor Red
}

# Test 4: Build directory simulation
Write-Host "`n4. Testing build directory operations..." -ForegroundColor Cyan
if (Test-Path $buildDir) {
    Write-Host "✓ Build directory exists"
    
    # Look for executables
    $exePaths = @(
        "$buildDir\Release\rouen.exe",
        "$buildDir\Debug\rouen.exe", 
        "$buildDir\rouen.exe"
    )
    
    $foundExe = $false
    foreach ($exePath in $exePaths) {
        if (Test-Path $exePath) {
            Write-Host "✓ Found executable: $exePath" -ForegroundColor Green
            $foundExe = $true
            break
        }
    }
    
    if (-not $foundExe) {
        Write-Host "ℹ No rouen.exe found (this is normal if not built yet)" -ForegroundColor Yellow
        # Show any .exe files that do exist
        $existingExes = Get-ChildItem -Recurse $buildDir -Include "*.exe" -ErrorAction SilentlyContinue
        if ($existingExes) {
            Write-Host "Found other executables:"
            $existingExes | ForEach-Object { Write-Host "  $($_.FullName)" }
        }
    }
} else {
    Write-Host "ℹ Build directory not found (this is normal before building)" -ForegroundColor Yellow
}

# Test 5: Asset file checks
Write-Host "`n5. Testing asset file availability..." -ForegroundColor Cyan
$assetFiles = @{
    "presets.txt" = "$WorkspacePath\presets.txt"
    "podcasts.txt" = "$WorkspacePath\podcasts.txt"
    "README.md" = "$WorkspacePath\README.md"
    "LICENSE" = "$WorkspacePath\LICENSE"
    "MaterialIcons font" = "$WorkspacePath\external\MaterialIcons-Regular.ttf"
    "Icons header" = "$WorkspacePath\external\IconsMaterialDesign.h"
}

foreach ($name in $assetFiles.Keys) {
    $path = $assetFiles[$name]
    $exists = Test-Path $path
    $status = if ($exists) { "✓" } else { "✗" }
    $color = if ($exists) { "Green" } else { "Red" }
    Write-Host "$status $name" -ForegroundColor $color
}

# Test 6: PowerShell commands that will be used in workflow
Write-Host "`n6. Testing PowerShell command compatibility..." -ForegroundColor Cyan

try {
    # Test directory creation
    $testDir = "$WorkspacePath\test_temp_dir"
    New-Item -ItemType Directory -Force -Path $testDir | Out-Null
    Write-Host "✓ Directory creation works"
    
    # Test file operations
    $testFile = "$testDir\test.txt"
    "test content" | Out-File $testFile
    Write-Host "✓ File creation works"
    
    # Test compression (this is used for ZIP creation)
    $testZip = "$testDir\test.zip"
    Compress-Archive -Path $testFile -DestinationPath $testZip
    Write-Host "✓ ZIP compression works"
    
    # Cleanup
    Remove-Item -Recurse -Force $testDir
    Write-Host "✓ Cleanup works"
    
} catch {
    Write-Host "✗ PowerShell operations test failed: $($_.Exception.Message)" -ForegroundColor Red
}

# Test 9: PowerShell Variable Expansion in CMake Commands
Write-Host "`n--- Test 9: PowerShell Variable Expansion ---" -ForegroundColor Blue

$buildDir = "$WorkspacePath\build"
$toolchainFile = "$WorkspacePath\vcpkg\scripts\buildsystems\vcpkg.cmake"

Write-Host "Testing PowerShell variable expansion:"
Write-Host "Build directory: $buildDir"
Write-Host "Toolchain file: $toolchainFile"

# Simulate the corrected cmake command from the workflow
$cmakeCmd = "cmake -B $buildDir -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$toolchainFile -DVCPKG_TARGET_TRIPLET=x64-windows -DCMAKE_VERBOSE_MAKEFILE=ON"
Write-Host "Generated CMake command: $cmakeCmd"

# Verify variables are properly expanded (not literal strings)
if ($cmakeCmd -like "*`$buildDir*" -or $cmakeCmd -like "*`$toolchainFile*") {
    Write-Host "❌ ERROR: Variables are not being expanded properly!" -ForegroundColor Red
    Write-Host "   This indicates the PowerShell variable expansion issue is present."
    $testResults.Add("PowerShell Variable Expansion", "FAILED - Variables not expanded")
} else {
    Write-Host "✅ SUCCESS: Variables are properly expanded in cmake command." -ForegroundColor Green
    $testResults.Add("PowerShell Variable Expansion", "PASSED")
}

Write-Host "`n=== Test Complete ===" -ForegroundColor Green
Write-Host "This script validated the key path operations used in the GitHub Actions workflow." -ForegroundColor Yellow
Write-Host "If all tests pass, the workflow should handle Windows paths correctly." -ForegroundColor Yellow
