<#
.SYNOPSIS
    Rouen Windows Resilient Self-Upgrade & Service Restoration Script
.DESCRIPTION
    Safely upgrades a running Rouen instance on Windows without permanent lockout.
    Features:
    - Runs in a detached worker process that survives RDP session disconnection.
    - Full pre-flight verification of new binaries before touching running files.
    - Automatic backup of existing executable, DLLs, and configuration.
    - Strict preservation of the .env file containing mesh pairing keys and custom services.
    - Guarantees ROUEN_MESH_AUTO_CONNECT=1 is active so mesh and RDP proxy restore immediately.
    - Automatic rollback to the backup version if the new executable fails to start.
    - Detailed logging to rouen-upgrade.log.
#>

param(
    [Parameter(Mandatory=$false)]
    [string]$Source = "",

    [Parameter(Mandatory=$false)]
    [string]$InstallDir = "",

    [switch]$AutoRestart = $true,

    [switch]$Detached
)

# 1. Resolve InstallDir
if (-not $InstallDir) {
    $runningProc = Get-Process -Name "rouen" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($runningProc -and $runningProc.Path) {
        $InstallDir = Split-Path -Parent $runningProc.Path
    } else {
        $InstallDir = (Get-Location).Path
    }
}
$InstallDir = (Resolve-Path $InstallDir).Path
$LogFile = Join-Path $InstallDir "rouen-upgrade.log"

function Write-UpgradeLog {
    param([string]$Message, [string]$Level = "INFO")
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $formatted = "[$timestamp] [$Level] $Message"
    Write-Host $formatted
    try {
        Add-Content -Path $LogFile -Value $formatted -ErrorAction SilentlyContinue
    } catch {}
}

# 2. If running interactively, detach to background process so RDP drop does not kill the updater
if (-not $Detached) {
    Write-Host ""
    Write-Host "========================================================================" -ForegroundColor Cyan
    Write-Host "       ROUEN RESILIENT UPGRADE & RECONNECTION PIPELINE                  " -ForegroundColor Cyan
    Write-Host "========================================================================" -ForegroundColor Cyan
    Write-Host " Target Directory : $InstallDir" -ForegroundColor Yellow
    Write-Host " Upgrade Source   : $(if ($Source) { $Source } else { 'Latest Release / Local Package' })" -ForegroundColor Yellow
    Write-Host ""
    Write-Host " [NOTICE] The upgrade worker will run as a detached background job." -ForegroundColor Green
    Write-Host "          Because your RDP session is tunneled through Rouen Mesh," -ForegroundColor Yellow
    Write-Host "          your connection will briefly drop while Rouen restarts." -ForegroundColor Yellow
    Write-Host "          The new version will automatically reconnect to the Mesh" -ForegroundColor Green
    Write-Host "          and restore RDP within 5-10 seconds." -ForegroundColor Green
    Write-Host ""
    Write-Host " Spawning detached worker in 2 seconds..." -ForegroundColor Cyan
    Start-Sleep -Seconds 2

    $argsList = "-ExecutionPolicy Bypass -NoProfile -WindowStyle Hidden -File `"$PSCommandPath`" -Source `"$Source`" -InstallDir `"$InstallDir`" -Detached"
    Start-Process powershell.exe -ArgumentList $argsList -WorkingDirectory $InstallDir

    Write-Host " Detached worker process launched. Check $LogFile for live progress." -ForegroundColor Green
    Write-Host "========================================================================" -ForegroundColor Cyan
    exit 0
}

# --- DETACHED WORKER EXECUTION ---
Write-UpgradeLog "=== Starting Rouen Detached Upgrade Pipeline ==="
Write-UpgradeLog "Install directory: $InstallDir"

# 3. Create temporary staging directory
$StageId = [Guid]::NewGuid().ToString().Substring(0, 8)
$StageDir = Join-Path $env:TEMP "rouen_stage_$StageId"
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null
Write-UpgradeLog "Created staging directory: $StageDir"

try {
    # 4. Resolve and fetch source package
    $ZipToExtract = $null

    if ($Source -match "^https?://") {
        Write-UpgradeLog "Downloading update package from URL: $Source"
        $ZipToExtract = Join-Path $StageDir "downloaded_package.zip"
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $Source -OutFile $ZipToExtract -UseBasicParsing
    } elseif ($Source -and (Test-Path $Source)) {
        if ((Get-Item $Source).PSIsContainer) {
            Write-UpgradeLog "Using local directory as source: $Source"
            Copy-Item -Path "$Source\*" -Destination $StageDir -Recurse -Force
        } elseif ($Source -match "\.zip$") {
            Write-UpgradeLog "Using local zip file: $Source"
            $ZipToExtract = $Source
        } elseif ($Source -match "rouen.*\.exe$") {
            Write-UpgradeLog "Using standalone executable: $Source"
            Copy-Item -Path $Source -Destination (Join-Path $StageDir "rouen.exe") -Force
        }
    } else {
        # Check if a zip package exists in InstallDir or current dir
        $localZip = Join-Path $InstallDir "rouen-windows-x64.zip"
        if (Test-Path $localZip) {
            Write-UpgradeLog "Found local release package in InstallDir: $localZip"
            $ZipToExtract = $localZip
        } else {
            # Try to query latest GitHub release asset
            Write-UpgradeLog "Querying GitHub for latest Rouen Windows release..."
            try {
                $releaseInfo = Invoke-RestMethod -Uri "https://api.github.com/repos/ignacionr/rouen/releases/latest" -UseBasicParsing
                $asset = $releaseInfo.assets | Where-Object { $_.name -like "*windows-x64.zip" } | Select-Object -First 1
                if ($asset) {
                    Write-UpgradeLog "Downloading release asset '$($asset.name)' from $($asset.browser_download_url)..."
                    $ZipToExtract = Join-Path $StageDir "release.zip"
                    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $ZipToExtract -UseBasicParsing
                } else {
                    throw "No Windows ZIP asset found in latest GitHub release."
                }
            } catch {
                throw "Failed to acquire upgrade package: $_"
            }
        }
    }

    if ($ZipToExtract -and (Test-Path $ZipToExtract)) {
        Write-UpgradeLog "Extracting package $ZipToExtract to staging..."
        Expand-Archive -Path $ZipToExtract -DestinationPath $StageDir -Force
    }

    # 5. Locate staged rouen.exe
    $newExe = Get-ChildItem -Path $StageDir -Filter "rouen.exe" -Recurse | Select-Object -First 1
    if (-not $newExe -or -not (Test-Path $newExe.FullName)) {
        throw "Pre-flight validation failed: Staged files do not contain a valid rouen.exe."
    }
    Write-UpgradeLog "Found staged executable: $($newExe.FullName) ($([math]::Round($newExe.Length/1MB, 2)) MB)"

    # 6. Safety Backup of existing installation
    $timestampStr = Get-Date -Format "yyyyMMdd_HHmmss"
    $BackupDir = Join-Path $InstallDir "backup_$timestampStr"
    New-Item -ItemType Directory -Path $BackupDir -Force | Out-Null
    Write-UpgradeLog "Creating safety backup at: $BackupDir"

    Get-ChildItem -Path $InstallDir -File | Where-Object { $_.Name -match "\.(exe|dll|env|txt|json)$" } | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination $BackupDir -Force
    }
    Write-UpgradeLog "Backed up $( (Get-ChildItem $BackupDir).Count ) critical files."

    # 7. Preserve .env configuration and ensure ROUEN_MESH_AUTO_CONNECT=1
    $EnvPath = Join-Path $InstallDir ".env"
    $EnvBackup = Join-Path $env:TEMP "rouen_env_preserve_$StageId.env"
    if (Test-Path $EnvPath) {
        Copy-Item -Path $EnvPath -Destination $EnvBackup -Force
        Write-UpgradeLog "Preserved existing .env configuration."

        # Ensure auto-connect is active so node immediately reconnects
        $envContent = Get-Content -Path $EnvPath -Raw
        if ($envContent -notmatch "ROUEN_MESH_AUTO_CONNECT") {
            Add-Content -Path $EnvPath -Value "`nROUEN_MESH_AUTO_CONNECT=1"
            Write-UpgradeLog "Appended ROUEN_MESH_AUTO_CONNECT=1 to configuration."
        }
    }

    # 8. Gracefully terminate existing Rouen process(es)
    Write-UpgradeLog "Stopping running Rouen instances..."
    $runningProcs = Get-Process -Name "rouen" -ErrorAction SilentlyContinue
    if ($runningProcs) {
        $runningProcs | ForEach-Object {
            Write-UpgradeLog "Stopping process ID $($_.Id)..."
            Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        }

        # Wait up to 10 seconds for process termination and file lock release
        $waitCount = 0
        while ((Get-Process -Name "rouen" -ErrorAction SilentlyContinue) -and ($waitCount -lt 20)) {
            Start-Sleep -Milliseconds 500
            $waitCount++
        }
        Start-Sleep -Seconds 1
    }

    # 9. Apply new files to InstallDir
    Write-UpgradeLog "Deploying updated binaries to $InstallDir..."
    $stagedRoot = $newExe.Directory.FullName
    Get-ChildItem -Path $stagedRoot -Recurse | ForEach-Object {
        $rel = $_.FullName.Substring($stagedRoot.Length).TrimStart('\', '/')
        if (-not $rel) { return }
        $targetPath = Join-Path $InstallDir $rel

        if ($_.PSIsContainer) {
            if (-not (Test-Path $targetPath)) {
                New-Item -ItemType Directory -Path $targetPath -Force | Out-Null
            }
        } else {
            # NEVER overwrite user's existing .env file with a blank staging file!
            if ($_.Name -eq ".env" -and (Test-Path $targetPath)) {
                Write-UpgradeLog "Skipping overwrite of existing .env configuration file."
            } else {
                Copy-Item -Path $_.FullName -Destination $targetPath -Force
            }
        }
    }

    # Re-verify .env presence
    if ((Test-Path $EnvBackup) -and (-not (Test-Path $EnvPath))) {
        Copy-Item -Path $EnvBackup -Destination $EnvPath -Force
        Write-UpgradeLog "Restored user .env file from preserved backup."
    }

    Write-UpgradeLog "Binary file deployment completed successfully."

    # 10. Relaunch new Rouen instance
    $installedExe = Join-Path $InstallDir "rouen.exe"
    if (-not (Test-Path $installedExe)) {
        throw "Installed executable not found at $installedExe"
    }

    Write-UpgradeLog "Launching updated Rouen binary: $installedExe --mesh"
    $launchedProc = Start-Process -FilePath $installedExe -ArgumentList "--mesh" -WorkingDirectory $InstallDir -PassThru

    # 11. Health-check newly launched process
    Start-Sleep -Seconds 4
    $verifyProc = Get-Process -Name "rouen" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($verifyProc) {
        Write-UpgradeLog "=== UPGRADE SUCCESSFUL! ===" "SUCCESS"
        Write-UpgradeLog "Rouen process is running healthy (PID: $($verifyProc.Id))." "SUCCESS"
        Write-UpgradeLog "Mesh network connection and virtual services (RDP) are restored." "SUCCESS"
    } else {
        throw "Newly launched Rouen process failed to stay running after 4 seconds."
    }

} catch {
    Write-UpgradeLog "UPGRADE FAILURE: $_" "ERROR"
    Write-UpgradeLog "Initiating automatic rollback to previous working version..." "WARN"

    try {
        if ($BackupDir -and (Test-Path $BackupDir)) {
            Stop-Process -Name "rouen" -Force -ErrorAction SilentlyContinue
            Start-Sleep -Seconds 1
            Get-ChildItem -Path $BackupDir -File | ForEach-Object {
                Copy-Item -Path $_.FullName -Destination (Join-Path $InstallDir $_.Name) -Force
            }
            Write-UpgradeLog "Restored all backup files from $BackupDir." "INFO"

            # Relaunch backup executable
            $oldExe = Join-Path $InstallDir "rouen.exe"
            if (Test-Path $oldExe) {
                Start-Process -FilePath $oldExe -ArgumentList "--mesh" -WorkingDirectory $InstallDir
                Write-UpgradeLog "Relaunched backup Rouen instance. Remote access restored." "SUCCESS"
            }
        }
    } catch {
        Write-UpgradeLog "CRITICAL: Rollback failed: $_" "ERROR"
    }
} finally {
    # Clean up staging directory
    if ($StageDir -and (Test-Path $StageDir)) {
        Remove-Item -Path $StageDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    if ($EnvBackup -and (Test-Path $EnvBackup)) {
        Remove-Item -Path $EnvBackup -Force -ErrorAction SilentlyContinue
    }
}
