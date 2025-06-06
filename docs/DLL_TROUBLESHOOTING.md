# DLL Dependency Troubleshooting Guide

## How to Identify Missing DLLs in MSI Installer

When the MSI installer creates successfully but the application fails to run due to missing DLLs, follow these steps:

### 1. Check Windows Event Viewer
- Look for application error events in Windows Event Viewer
- Note the specific DLL names mentioned in the error

### 2. Use Dependency Walker (depends.exe)
```
1. Download Dependency Walker from dependencywalker.org
2. Open rouen.exe in Dependency Walker
3. Look for red entries indicating missing DLLs
4. Note any DLLs marked as "Error opening file"
```

### 3. Use PowerShell to List Dependencies
```powershell
# Check what DLLs are actually in the build directory
Get-ChildItem "dist\*.dll" | Sort-Object Name | Format-Table Name, Length

# Compare with MSI contents using 7-Zip or similar
```

### 4. Update WiX Configuration

Add missing DLLs to `installer/rouen.wxs` in the Dependencies component:

```xml
<Component Id="Dependencies" Guid="12345678-1234-1234-1234-123456789014">
  <!-- ...existing dependencies... -->
  
  <!-- Add new DLL here -->
  <File Id="NewDLL" Source="$(var.SourceDir)\newdll.dll" />
</Component>
```

### 5. Update Dynamic MSI Builder

Add to the `$ExpectedDLLs` hashtable in `installer/build-msi-dynamic.ps1`:

```powershell
"newdll.dll" = @{ Category = "Graphics"; Description = "New library"; Required = $true }
```

### 6. Common Missing DLLs

Based on vcpkg dependencies, commonly missed DLLs include:

**Graphics Libraries:**
- `libpng16.dll` - PNG image support
- `jpeg62.dll` - JPEG image support  
- `libtiff.dll` - TIFF image support
- `libwebp.dll` - WebP image support

**Database Libraries:**
- `sqlite3.dll` - SQLite database

**Compression Libraries:**
- `zlib1.dll` - Compression support
- `bzip2.dll` - BZip2 compression

**Format Libraries:**
- `fmt.dll` - String formatting
- `tinyxml2.dll` - XML parsing

### 7. Testing Process

1. **Build locally** with all dependencies
2. **Copy all DLLs** from the build directory to a test directory  
3. **Run rouen.exe** from the test directory to verify all dependencies
4. **Update MSI configuration** to include all required DLLs
5. **Test MSI installer** on a clean Windows system

### 8. Validation Commands

```bash
# Run validation script
bash scripts/validate-msi-config.sh

# Check for specific DLL references
grep -i "sqlite3\|libpng16\|jpeg" installer/rouen.wxs
```

### 9. GitHub Actions Debugging

If the MSI builds but has missing DLLs:

1. **Download the build artifacts** from GitHub Actions
2. **Extract and examine** the contents of both ZIP and MSI
3. **Compare DLL lists** to identify missing files
4. **Add missing DLLs** to the WiX configuration
5. **Push changes** to trigger a new build

This process ensures that all runtime dependencies are properly included in the MSI installer package.
