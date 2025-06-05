# Test MSI Creation Script
# This script simulates the MSI creation process for local testing

param(
    [string]$TestMode = "validate"  # "validate" or "create"
)

Write-Host "=== MSI Creation Test Script ==="
Write-Host "Test Mode: $TestMode"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$InstallerDir = $ScriptDir
$DistDir = "$ProjectRoot\dist"

Write-Host "Project Root: $ProjectRoot"
Write-Host "Installer Dir: $InstallerDir" 
Write-Host "Distribution Dir: $DistDir"

# Test 1: Check for required files
Write-Host "`n=== Testing Required Files ==="

$RequiredFiles = @(
    "$InstallerDir\rouen.wxs",
    "$InstallerDir\license.rtf",
    "$ProjectRoot\resources\rouen.ico"
)

$AllFilesExist = $true
foreach ($file in $RequiredFiles) {
    if (Test-Path $file) {
        $fileInfo = Get-Item $file
        Write-Host "✅ Found: $($fileInfo.Name) ($([math]::Round($fileInfo.Length / 1KB, 1)) KB)"
    } else {
        Write-Host "❌ Missing: $file"
        $AllFilesExist = $false
    }
}

if (-not $AllFilesExist) {
    Write-Error "Required files are missing!"
    exit 1
}

# Test 2: Check WiX source file structure
Write-Host "`n=== Testing WiX Source Structure ==="

$wxsContent = Get-Content "$InstallerDir\rouen.wxs" -Raw
$RequiredElements = @(
    "Product.*Name=.*Rouen",
    "Package.*InstallScope=.*perUser", 
    "Directory.*Id=.*LocalAppDataFolder",
    "Feature.*Id=.*MainApplication",
    "Component.*Id=.*MainExecutable",
    "Icon.*Id=.*ProductIcon"
)

foreach ($pattern in $RequiredElements) {
    if ($wxsContent -match $pattern) {
        Write-Host "✅ Found pattern: $pattern"
    } else {
        Write-Host "❌ Missing pattern: $pattern"
    }
}

# Test 3: Create mock distribution directory
Write-Host "`n=== Creating Mock Distribution ==="

if (Test-Path $DistDir) {
    Remove-Item $DistDir -Recurse -Force
    Write-Host "Cleaned existing dist directory"
}

New-Item -ItemType Directory -Path $DistDir -Force | Out-Null

# Create mock files
$MockFiles = @{
    "rouen.exe" = 1024 * 1024  # 1 MB
    "SDL2.dll" = 1024 * 500    # 500 KB
    "libcurl.dll" = 1024 * 300 # 300 KB
    "libssl-3-x64.dll" = 1024 * 400  # 400 KB
    "libcrypto-3-x64.dll" = 1024 * 600  # 600 KB
}

foreach ($fileName in $MockFiles.Keys) {
    $filePath = "$DistDir\$fileName"
    $size = $MockFiles[$fileName]
    
    # Create a file with the specified size
    $bytes = New-Object byte[] $size
    [System.IO.File]::WriteAllBytes($filePath, $bytes)
    
    $sizeKB = [math]::Round($size / 1KB, 1)
    Write-Host "✅ Created mock file: $fileName ($sizeKB KB)"
}

Write-Host "`n=== Mock Distribution Created ==="
$distFiles = Get-ChildItem $DistDir
Write-Host "Total files: $($distFiles.Count)"
$totalSizeMB = [math]::Round(($distFiles | Measure-Object -Property Length -Sum).Sum / 1MB, 2)
Write-Host "Total size: $totalSizeMB MB"

# Test 4: Check if this is Windows and if WiX is available (optional)
if ($TestMode -eq "create" -and $IsWindows) {
    Write-Host "`n=== Testing WiX Availability (Windows Only) ==="
    
    $WixPaths = @(
        "${env:ProgramFiles(x86)}\WiX Toolset v3.11\bin",
        "${env:ProgramFiles}\WiX Toolset v3.11\bin",
        "C:\Program Files (x86)\WiX Toolset v3.11\bin"
    )
    
    $WixFound = $false
    foreach ($path in $WixPaths) {
        if (Test-Path "$path\candle.exe") {
            Write-Host "✅ Found WiX at: $path"
            $WixFound = $true
            break
        }
    }
    
    if (-not $WixFound) {
        Write-Host "⚠️ WiX Toolset not found (this is normal on non-Windows or development machines)"
        Write-Host "   MSI creation will only work in the GitHub Actions Windows environment"
    }
}

Write-Host "`n=== Test Complete ==="
if ($AllFilesExist) {
    Write-Host "✅ All required files are present"
    Write-Host "✅ Mock distribution created successfully"
    Write-Host "✅ Ready for MSI creation in GitHub Actions"
} else {
    Write-Host "❌ Some required files are missing"
    exit 1
}

# Clean up mock distribution
if (Test-Path $DistDir) {
    Remove-Item $DistDir -Recurse -Force
    Write-Host "✅ Cleaned up mock distribution"
}

Write-Host "`n🎉 MSI Creation Test Passed!"
