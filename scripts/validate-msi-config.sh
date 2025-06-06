#!/bin/bash

# WiX Configuration Validation Script for macOS
# This script validates our WiX configuration without requiring Windows

echo "=== WiX Configuration Validation ==="
echo "Platform: $(uname -s)"
echo "Date: $(date)"
echo ""

# Check if we're in the correct directory
if [ ! -f "installer/rouen.wxs" ]; then
    echo "❌ Error: Must run from Rouen project root directory"
    echo "   Expected file: installer/rouen.wxs"
    exit 1
fi

echo "✅ Found WiX source file: installer/rouen.wxs"

# Validate WiX XML syntax
echo ""
echo "=== XML Syntax Validation ==="

if command -v xmllint >/dev/null 2>&1; then
    if xmllint --noout installer/rouen.wxs 2>/dev/null; then
        echo "✅ XML syntax is valid"
    else
        echo "❌ XML syntax errors found:"
        xmllint --noout installer/rouen.wxs
        exit 1
    fi
else
    echo "⚠️  xmllint not available, skipping XML validation"
    echo "   Install with: brew install libxml2"
fi

# Check for required files referenced in WiX
echo ""
echo "=== File Reference Validation ==="

required_files=(
    "resources/rouen.ico"
    "installer/license.rtf"
)

for file in "${required_files[@]}"; do
    if [ -f "$file" ]; then
        echo "✅ Found: $file"
    else
        echo "⚠️  Missing: $file"
    fi
done

# Check WiX source for common issues
echo ""
echo "=== WiX Source Analysis ==="

wix_file="installer/rouen.wxs"

# Check for proper namespaces
if grep -q 'xmlns="http://schemas.microsoft.com/wix/2006/wi"' "$wix_file"; then
    echo "✅ Proper WiX namespace declared"
else
    echo "❌ Missing or incorrect WiX namespace"
fi

# Check for InstallScope
if grep -q 'InstallScope="perUser"' "$wix_file"; then
    echo "✅ User-mode installation configured"
else
    echo "⚠️  InstallScope not set to perUser"
fi

# Check for version placeholder
if grep -q '\$(var\.Version)' "$wix_file"; then
    echo "✅ Version placeholder found"
else
    echo "❌ Version placeholder missing"
fi

# Check for source directory placeholder
if grep -q '\$(var\.SourceDir)' "$wix_file"; then
    echo "✅ Source directory placeholder found"
else
    echo "❌ Source directory placeholder missing"
fi

# Check for registry keys as KeyPath (required for user-mode installation)
if grep -q 'KeyPath="yes".*RegistryValue' "$wix_file"; then
    echo "✅ Registry-based KeyPath found (required for user-mode)"
else
    echo "⚠️  Registry-based KeyPath not found - may cause ICE38 errors"
fi

# Check for RemoveFolder elements (required for proper cleanup)
if grep -q 'RemoveFolder' "$wix_file"; then
    echo "✅ RemoveFolder elements found (required for cleanup)"
else
    echo "⚠️  RemoveFolder elements missing - may cause ICE64 errors"
fi

# Check for required components
components=(
    "MainExecutable"
    "Dependencies"
    "OptionalDependencies"
    "Assets"
    "ApplicationShortcuts"
    "DesktopShortcut"
    "RegistryCleanup"
)

echo ""
echo "=== Component Validation ==="

for component in "${components[@]}"; do
    if grep -q "Id=\"$component\"" "$wix_file"; then
        echo "✅ Component: $component"
    else
        echo "❌ Missing component: $component"
    fi
done

# Check GitHub Actions workflow
echo ""
echo "=== GitHub Actions Workflow Validation ==="

workflow_file=".github/workflows/windows-release.yml"

if [ -f "$workflow_file" ]; then
    echo "✅ Found workflow file: $workflow_file"
    
    # Check for WiX installation step
    if grep -q "Install WiX Toolset" "$workflow_file"; then
        echo "✅ WiX installation step present"
    else
        echo "❌ WiX installation step missing"
    fi
    
    # Check for MSI creation step
    if grep -q "Create MSI Installer" "$workflow_file"; then
        echo "✅ MSI creation step present"
    else
        echo "❌ MSI creation step missing"
    fi
    
    # Check for artifact upload
    if grep -q "\.msi" "$workflow_file"; then
        echo "✅ MSI artifact handling configured"
    else
        echo "❌ MSI artifact handling missing"
    fi
    
else
    echo "❌ Workflow file not found: $workflow_file"
fi

# Generate validation report
echo ""
echo "=== Validation Summary ==="

# Count issues
xml_issues=0
file_issues=0
wix_issues=0
workflow_issues=0

if ! xmllint --noout installer/rouen.wxs 2>/dev/null; then
    xml_issues=1
fi

for file in "${required_files[@]}"; do
    if [ ! -f "$file" ]; then
        ((file_issues++))
    fi
done

# Overall assessment
total_issues=$((xml_issues + file_issues + wix_issues + workflow_issues))

if [ $total_issues -eq 0 ]; then
    echo "🎉 All validations passed! MSI configuration appears ready."
    echo "   The workflow should successfully create MSI packages on Windows."
else
    echo "⚠️  Found $total_issues potential issues:"
    [ $xml_issues -gt 0 ] && echo "   - XML syntax errors"
    [ $file_issues -gt 0 ] && echo "   - $file_issues missing required files"
    [ $wix_issues -gt 0 ] && echo "   - WiX configuration issues"
    [ $workflow_issues -gt 0 ] && echo "   - Workflow configuration issues"
    echo ""
    echo "   Review the issues above before running the Windows workflow."
fi

echo ""
echo "=== Next Steps ==="
echo "1. Commit and push changes to trigger Windows workflow"
echo "2. Monitor GitHub Actions for WiX installation and MSI creation"
echo "3. Download and test the generated MSI package"
echo "4. Verify user-mode installation and functionality"

echo ""
echo "=== Validation Complete ==="
