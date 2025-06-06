#!/bin/bash

# GitHub Actions Workflow Status Checker
# Helps monitor the Windows Release workflow progress

echo "=== GitHub Actions Workflow Status ==="
echo "Repository: ignacionr/rouen"
echo "Branch: feature/windows-installer"
echo "Date: $(date)"
echo ""

# Check if we're in a git repository
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    echo "❌ Error: Not in a git repository"
    exit 1
fi

# Get the latest commit hash
LATEST_COMMIT=$(git rev-parse HEAD)
BRANCH_NAME=$(git branch --show-current)

echo "📋 Latest commit: ${LATEST_COMMIT:0:8}"
echo "🌿 Current branch: $BRANCH_NAME"
echo ""

# Check GitHub CLI availability
if command -v gh >/dev/null 2>&1; then
    echo "🔍 Checking workflow runs with GitHub CLI..."
    echo ""
    
    # Get recent workflow runs
    gh run list --limit 5 --branch "$BRANCH_NAME" --workflow "Windows Release"
    
    echo ""
    echo "💡 To view detailed logs of the latest run:"
    echo "   gh run view --log"
    echo ""
    echo "💡 To watch the latest run in real-time:"
    echo "   gh run watch"
    
else
    echo "⚠️  GitHub CLI not available"
    echo "   Install with: brew install gh"
    echo "   Then authenticate: gh auth login"
    echo ""
    echo "🌐 Check workflow status manually at:"
    echo "   https://github.com/ignacionr/rouen/actions"
fi

echo ""
echo "📦 Expected Artifacts:"
echo "   • Rouen-1.0.{run_number}.0-x64.zip"
echo "   • Rouen-1.0.{run_number}.0-x64.msi"
echo ""

echo "🎯 What to look for in the workflow:"
echo "   ✅ WiX Toolset installation successful"
echo "   ✅ MSI compilation (candle.exe) completes without ICE errors"
echo "   ✅ MSI linking (light.exe) creates valid MSI package"
echo "   ✅ Artifacts uploaded to GitHub"
echo ""

echo "🔧 If the workflow fails:"
echo "   1. Check WiX installation step output"
echo "   2. Look for ICE validation errors in light.exe output"
echo "   3. Verify file paths and dependencies"
echo "   4. Review our validation script: scripts/validate-msi-config.sh"
echo ""

echo "=== Status Check Complete ==="
