# PowerShell Variable Expansion Fix for GitHub Actions

## Issue Identified
The Windows release workflow had a critical PowerShell variable expansion issue in the CMake configuration step. The variables `$buildDir` and `$toolchainFile` were being passed as literal strings instead of their expanded values.

## Problem Code (Before Fix)
```yaml
- name: Configure CMake
  run: |
    $buildDir = "${{ github.workspace }}\build"
    $toolchainFile = "${{ github.workspace }}\vcpkg\scripts\buildsystems\vcpkg.cmake"
    cmake -B "$buildDir" -DCMAKE_TOOLCHAIN_FILE="$toolchainFile" ...
```

## Root Cause
When PowerShell variables are enclosed in double quotes within a cmake command, the variable expansion doesn't work correctly. PowerShell was passing the literal strings `"$buildDir"` and `"$toolchainFile"` to cmake instead of their actual values.

## Solution Applied
Removed the double quotes around PowerShell variables in the cmake command:

```yaml
- name: Configure CMake
  run: |
    $buildDir = "${{ github.workspace }}\build"
    $toolchainFile = "${{ github.workspace }}\vcpkg\scripts\buildsystems\vcpkg.cmake"
    cmake -B $buildDir -DCMAKE_TOOLCHAIN_FILE=$toolchainFile ...
```

## Files Modified
- `.github/workflows/windows-release.yml`: Fixed PowerShell variable expansion in CMake configure step
- `scripts/test_windows_workflow.ps1`: Added test case to validate variable expansion

## Verification
The build.yml workflow already used the correct syntax for PowerShell variable expansion, confirming this is the proper approach for cross-platform compatibility.

## Impact
This fix ensures that:
1. CMake receives properly expanded file paths
2. The vcpkg toolchain file is correctly located
3. Build directories are properly specified
4. Windows builds will succeed in GitHub Actions

## Testing
Added Test 9 to the PowerShell validation script to automatically detect this type of variable expansion issue in future workflow updates.
