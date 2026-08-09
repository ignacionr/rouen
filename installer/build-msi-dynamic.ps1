# Dynamic MSI Builder for Rouen
# This script creates a WiX source file based on the actual DLLs present in the distribution directory

param(
    [Parameter(Mandatory=$true)]
    [string]$SourceDir,
    
    [Parameter(Mandatory=$true)]
    [string]$Version,
    
    [Parameter(Mandatory=$true)]
    [string]$OutputPath,
    
    [string]$WixPath = "${env:ProgramFiles(x86)}\WiX Toolset v3.11\bin"
)

Write-Host "=== Dynamic MSI Builder for Rouen ==="
Write-Host "Source Directory: $SourceDir"
Write-Host "Version: $Version"
Write-Host "Output Path: $OutputPath"
Write-Host "WiX Path: $WixPath"

# Verify source directory exists
if (-not (Test-Path $SourceDir)) {
    Write-Error "Source directory not found: $SourceDir"
    exit 1
}

# Verify WiX tools exist
$candleExe = "$WixPath\candle.exe"
$lightExe = "$WixPath\light.exe"

if (-not (Test-Path $candleExe)) {
    Write-Error "candle.exe not found at: $candleExe"
    exit 1
}

if (-not (Test-Path $lightExe)) {
    Write-Error "light.exe not found at: $lightExe"
    exit 1
}

# Define expected DLLs and their categories
$ExpectedDLLs = @{
    # Network libraries
    "libcurl.dll" = @{ Category = "Network"; Description = "Network HTTP/HTTPS library"; Required = $true }
    "libssl-3-x64.dll" = @{ Category = "Network"; Description = "SSL/TLS encryption library"; Required = $true }
    "libcrypto-3-x64.dll" = @{ Category = "Network"; Description = "Cryptographic library"; Required = $true }
    
    # Graphics and UI libraries
    "SDL3.dll" = @{ Category = "Graphics"; Description = "SDL3 multimedia library"; Required = $true }
    "SDL3_image.dll" = @{ Category = "Graphics"; Description = "SDL3 image loading library"; Required = $false }
    "libpng16.dll" = @{ Category = "Graphics"; Description = "PNG image library"; Required = $true }
    "jpeg62.dll" = @{ Category = "Graphics"; Description = "JPEG image library"; Required = $false }
    
    # Database libraries
    "sqlite3.dll" = @{ Category = "Database"; Description = "SQLite database library"; Required = $true }
    
    # Utility libraries
    "tinyxml2.dll" = @{ Category = "Utility"; Description = "XML parsing library"; Required = $false }
    "zlib1.dll" = @{ Category = "Utility"; Description = "Compression library"; Required = $false }
    
    # Visual C++ Runtime
    "vcruntime140.dll" = @{ Category = "Runtime"; Description = "Visual C++ Runtime"; Required = $true }
    "vcruntime140_1.dll" = @{ Category = "Runtime"; Description = "Visual C++ Runtime"; Required = $true }
    "msvcp140.dll" = @{ Category = "Runtime"; Description = "Visual C++ Standard Library"; Required = $true }
    "concrt140.dll" = @{ Category = "Runtime"; Description = "Visual C++ Concurrency Runtime"; Required = $false }
    "msvcp140_1.dll" = @{ Category = "Runtime"; Description = "Visual C++ Standard Library"; Required = $false }
    "msvcp140_2.dll" = @{ Category = "Runtime"; Description = "Visual C++ Standard Library"; Required = $false }
}

# Scan for actual DLLs present
$PresentDLLs = @{}
$AllDLLs = Get-ChildItem "$SourceDir\*.dll" -ErrorAction SilentlyContinue

Write-Host "`n=== Scanning for DLL Dependencies ==="
Write-Host "Found $($AllDLLs.Count) DLL files in source directory"

foreach ($dll in $AllDLLs) {
    $dllName = $dll.Name.ToLower()
    $sizeKB = [math]::Round($dll.Length / 1KB, 1)
    
    if ($ExpectedDLLs.ContainsKey($dllName)) {
        $info = $ExpectedDLLs[$dllName]
        $PresentDLLs[$dllName] = @{
            Path = $dll.FullName
            Size = $dll.Length
            Category = $info.Category
            Description = $info.Description
            Required = $info.Required
        }
        $status = if ($info.Required) { "✅ REQUIRED" } else { "📦 OPTIONAL" }
        Write-Host "  $status $dllName ($sizeKB KB) - $($info.Description)"
    } else {
        $PresentDLLs[$dllName] = @{
            Path = $dll.FullName
            Size = $dll.Length
            Category = "Unknown"
            Description = "Additional library"
            Required = $false
        }
        Write-Host "  📄 ADDITIONAL $dllName ($sizeKB KB) - Additional library"
    }
}

# Check for missing required DLLs
$MissingRequired = @()
foreach ($dllName in $ExpectedDLLs.Keys) {
    $info = $ExpectedDLLs[$dllName]
    if ($info.Required -and -not $PresentDLLs.ContainsKey($dllName)) {
        $MissingRequired += $dllName
    }
}

if ($MissingRequired.Count -gt 0) {
    Write-Host "`n⚠️ WARNING: Missing required DLLs:"
    foreach ($dll in $MissingRequired) {
        Write-Host "  ❌ $dll - $($ExpectedDLLs[$dll].Description)"
    }
    Write-Host "`nContinuing anyway, but the application may not work correctly..."
}

# Generate GUID helper function
function New-Guid {
    return [System.Guid]::NewGuid().ToString().ToUpper()
}

# Generate dynamic WiX source
$WixSource = @"
<?xml version="1.0" encoding="UTF-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
  <Product Id="*" Name="Rouen" Language="1033" Version="$Version" Manufacturer="Rouen Development Team" UpgradeCode="12345678-1234-1234-1234-123456789012">
    
    <Package InstallerVersion="200" Compressed="yes" InstallScope="perUser" Description="Rouen - Modern Productivity Dashboard" />
    
    <!-- Define the product properties -->
    <Property Id="ARPPRODUCTICON" Value="ProductIcon" />
    <Property Id="ARPHELPLINK" Value="https://github.com/ignaciorodriguez/rouen" />
    <Property Id="ARPURLINFOABOUT" Value="https://github.com/ignaciorodriguez/rouen" />
    <Property Id="ARPNOREPAIR" Value="1" />
    <Property Id="ARPNOMODIFY" Value="1" />
    
    <!-- Define the media and directory structure -->
    <MediaTemplate EmbedCab="yes" />
    
    <!-- Directory structure -->
    <Directory Id="TARGETDIR" Name="SourceDir">
      <Directory Id="LocalAppDataFolder">
        <Directory Id="APPLICATIONFOLDER" Name="Rouen">
          <Directory Id="INSTALLFOLDER" Name="Rouen" />
        </Directory>
      </Directory>
      <Directory Id="ProgramMenuFolder">
        <Directory Id="ApplicationProgramsFolder" Name="Rouen" />
      </Directory>
      <Directory Id="DesktopFolder" />
    </Directory>
    
    <!-- Feature definition -->
    <Feature Id="MainApplication" Title="Rouen Application" Level="1">
      <ComponentRef Id="MainExecutable" />
      <ComponentRef Id="ApplicationShortcuts" />
      <ComponentRef Id="DesktopShortcut" />
"@

# Group DLLs by category and add components
$Categories = $PresentDLLs.Values | Group-Object Category | Sort-Object Name

foreach ($category in $Categories) {
    $componentId = "Dependencies_$($category.Name)"
    $WixSource += "`n      <ComponentRef Id=`"$componentId`" />"
}

$WixSource += @"

      <ComponentRef Id="Assets" />
    </Feature>
    
    <!-- Main executable component -->
    <DirectoryRef Id="INSTALLFOLDER">
      <Component Id="MainExecutable" Guid="$(New-Guid)">
        <File Id="RouenExe" Source="`$(var.SourceDir)\rouen.exe" KeyPath="yes">
          <Shortcut Id="ApplicationStartMenuShortcut" Directory="ApplicationProgramsFolder" Name="Rouen" WorkingDirectory="INSTALLFOLDER" Icon="ProductIcon" IconIndex="0" />
          <Shortcut Id="ApplicationDesktopShortcut" Directory="DesktopFolder" Name="Rouen" WorkingDirectory="INSTALLFOLDER" Icon="ProductIcon" IconIndex="0" />
        </File>
      </Component>
      
"@

# Add DLL components by category
foreach ($category in $Categories) {
    $componentId = "Dependencies_$($category.Name)"
    $WixSource += "      <!-- $($category.Name) Dependencies Component -->`n"
    $WixSource += "      <Component Id=`"$componentId`" Guid=`"$(New-Guid)`">`n"
    
    $dlls = $category.Group | Sort-Object { $_.Path }
    foreach ($dll in $dlls) {
        $fileName = [System.IO.Path]::GetFileName($dll.Path)
        $fileId = ($fileName -replace '\.|_|-', '').Replace('dll', 'DLL')
        $WixSource += "        <File Id=`"$fileId`" Source=`"`$(var.SourceDir)\$fileName`" />`n"
    }
    
    $WixSource += "      </Component>`n      `n"
}

# Add assets component
$WixSource += @"
      <!-- Assets component -->
      <Component Id="Assets" Guid="$(New-Guid)">
        <File Id="DependenciesManifest" Source="`$(var.SourceDir)\DEPENDENCIES.txt" />
        <File Id="License" Source="`$(var.SourceDir)\LICENSE" />
        <File Id="ReadMe" Source="`$(var.SourceDir)\README.md" />
      </Component>
    </DirectoryRef>
    
    <!-- Application shortcuts component -->
    <DirectoryRef Id="ApplicationProgramsFolder">
      <Component Id="ApplicationShortcuts" Guid="$(New-Guid)">
        <Shortcut Id="UninstallProduct" Name="Uninstall Rouen" Target="[SystemFolder]msiexec.exe" Arguments="/x [ProductCode]" />
        <RemoveFolder Id="ApplicationProgramsFolder" On="uninstall" />
        <RegistryValue Root="HKCU" Key="Software\Rouen\Installed" Name="installed" Type="integer" Value="1" KeyPath="yes" />
      </Component>
    </DirectoryRef>
    
    <!-- Desktop shortcut component -->
    <DirectoryRef Id="DesktopFolder">
      <Component Id="DesktopShortcut" Guid="$(New-Guid)">
        <RegistryValue Root="HKCU" Key="Software\Rouen\Desktop" Name="installed" Type="integer" Value="1" KeyPath="yes" />
      </Component>
    </DirectoryRef>
    
    <!-- Define the application icon -->
    <Icon Id="ProductIcon" SourceFile="`$(var.SourceDir)\..\resources\rouen.ico" />
    
    <!-- UI definition for user-mode installer -->
    <UIRef Id="WixUI_InstallDir" />
    <Property Id="WIXUI_INSTALLDIR" Value="INSTALLFOLDER" />
    
    <!-- License file -->
    <WixVariable Id="WixUILicenseRtf" Value="`$(var.SourceDir)\..\installer\license.rtf" />
    
    <!-- Upgrade logic -->
    <MajorUpgrade DowngradeErrorMessage="A newer version of Rouen is already installed." />
    
  </Product>
</Wix>
"@

# Write the dynamic WiX source file
$DynamicWxsPath = "$env:TEMP\rouen-dynamic.wxs"
$WixSource | Out-File -FilePath $DynamicWxsPath -Encoding UTF8

Write-Host "`n=== Generated Dynamic WiX Source ==="
Write-Host "WiX source file: $DynamicWxsPath"
Write-Host "Components generated:"
foreach ($category in $Categories) {
    $count = $category.Group.Count
    Write-Host "  - $($category.Name): $count DLLs"
}

# Compile and link
$WixObjPath = "$env:TEMP\rouen-dynamic.wixobj"

Write-Host "`n=== Compiling WiX Source ==="
$candleArgs = @(
    "-out", $WixObjPath,
    "-dSourceDir=$SourceDir",
    "-dVersion=$Version",
    $DynamicWxsPath
)

Write-Host "Running: candle.exe $($candleArgs -join ' ')"
$candleResult = & $candleExe @candleArgs 2>&1
$candleExitCode = $LASTEXITCODE

if ($candleExitCode -ne 0) {
    Write-Host "Candle output:"
    Write-Host $candleResult
    Write-Error "WiX compilation failed with exit code: $candleExitCode"
    exit 1
}

Write-Host "`n=== Creating MSI Package ==="
$lightArgs = @(
    "-out", $OutputPath,
    "-ext", "WixUIExtension",
    $WixObjPath
)

Write-Host "Running: light.exe $($lightArgs -join ' ')"
$lightResult = & $lightExe @lightArgs 2>&1
$lightExitCode = $LASTEXITCODE

if ($lightExitCode -ne 0) {
    Write-Host "Light output:"
    Write-Host $lightResult
    Write-Error "MSI creation failed with exit code: $lightExitCode"
    exit 1
}

# Verify and report success
if (Test-Path $OutputPath) {
    $msiInfo = Get-Item $OutputPath
    $sizeMB = [math]::Round($msiInfo.Length / 1MB, 2)
    
    Write-Host "`n✅ MSI Package Created Successfully!"
    Write-Host "File: $($msiInfo.Name)"
    Write-Host "Size: $sizeMB MB"
    Write-Host "Path: $($msiInfo.FullName)"
    Write-Host "Created: $($msiInfo.CreationTime)"
    
    Write-Host "`n📊 Package Contents Summary:"
    Write-Host "  - Main executable: rouen.exe"
    Write-Host "  - Total DLLs: $($PresentDLLs.Count)"
    Write-Host "  - Required DLLs present: $(($PresentDLLs.Values | Where-Object { $_.Required }).Count)"
    Write-Host "  - Optional DLLs present: $(($PresentDLLs.Values | Where-Object { -not $_.Required }).Count)"
    Write-Host "  - Installation scope: Per-user"
    Write-Host "  - UI: Full installer wizard"
    
} else {
    Write-Error "MSI file was not created: $OutputPath"
    exit 1
}

# Clean up temporary files
if (Test-Path $DynamicWxsPath) { Remove-Item $DynamicWxsPath -Force }
if (Test-Path $WixObjPath) { Remove-Item $WixObjPath -Force }

Write-Host "`n🎉 Dynamic MSI creation completed successfully!"
