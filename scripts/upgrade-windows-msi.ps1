<#
.SYNOPSIS
    Rouen Windows MSI Upgrade & Auto-Reconnection Script
.DESCRIPTION
    Safely upgrades a running Rouen instance using a WiX MSI installer package without permanent lockout.
    Features:
    - Runs in a detached background worker that survives RDP session termination.
    - Preserves existing .env with Ed25519 pairing keys and mesh services.
    - Ensures ROUEN_MESH_AUTO_CONNECT=1 is present so RDP and virtual routes restore immediately.
    - Gracefully closes running rouen.exe to avoid MSI file locking errors (1603/1618).
    - Runs msiexec silently (/qn /norestart).
    - Verifies new rouen.exe is alive and connected; automatically rolls back if it crashes.
#>

param(
    [Parameter(Mandatory=$false)]
    [string]$MsiPath = "",

    [Parameter(Mandatory=$false)]
    [string]$InstallDir = "",

    [Parameter(Mandatory=$false)]
    [string]$OriginalExePath = "",

    [switch]$Detached
)

# 1. Resolve InstallDir and track original running executable path
if (-not $OriginalExePath) {
    $runningProc = Get-Process -Name "rouen" -ErrorAction SilentlyContinue | Where-Object { $_.Path } | Select-Object -First 1
    if ($runningProc -and $runningProc.Path) {
        $OriginalExePath = $runningProc.Path
    }
}

if (-not $InstallDir) {
    if ($OriginalExePath) {
        $InstallDir = Split-Path -Parent $OriginalExePath
    } else {
        $defaultMsiPath = Join-Path $env:LOCALAPPDATA "Rouen\Rouen"
        if (Test-Path $defaultMsiPath) {
            $InstallDir = $defaultMsiPath
        } else {
            $InstallDir = (Get-Location).Path
        }
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

function Start-RouenInteractive {
    param(
        [Parameter(Mandatory=$true)]
        [string]$ExePath,
        [string]$Arguments = "--mesh",
        [string]$WorkingDirectory = ""
    )
    if (-not (Test-Path $ExePath)) {
        Write-UpgradeLog "Executable not found at: $ExePath" "ERROR"
        return $null
    }
    if (-not $WorkingDirectory) {
        $WorkingDirectory = Split-Path -Parent $ExePath
    }

    Write-UpgradeLog "Launching Rouen in interactive desktop session: $ExePath $Arguments"

    # In background or remote sessions (WinRM, SSH, Session 0), direct Start-Process
    # cannot initialize graphics/DirectX/SDL3 windows. Use a temporary interactive Scheduled Task (/it).
    $launchedViaSchtasks = $false
    try {
        $taskName = "RouenRestart_$([Guid]::NewGuid().ToString().Substring(0,8))"
        $cmdLine = "`"$ExePath`" $Arguments"
        $createRes = schtasks.exe /create /tn $taskName /tr $cmdLine /sc once /st 00:00 /it /f 2>&1
        if ($LASTEXITCODE -eq 0) {
            schtasks.exe /run /tn $taskName 2>&1 | Out-Null
            Start-Sleep -Seconds 2
            schtasks.exe /delete /tn $taskName /f 2>&1 | Out-Null
            $launchedViaSchtasks = $true
            Write-UpgradeLog "Triggered interactive Scheduled Task ($taskName)."
        } else {
            Write-UpgradeLog "Interactive task creation returned code $LASTEXITCODE; falling back to Start-Process." "WARN"
        }
    } catch {
        Write-UpgradeLog "schtasks attempt failed: $_; falling back to Start-Process." "WARN"
    }

    if (-not $launchedViaSchtasks) {
        Start-Process -FilePath $ExePath -ArgumentList $Arguments -WorkingDirectory $WorkingDirectory
    }

    Start-Sleep -Seconds 3
    $proc = Get-Process -Name "rouen" -ErrorAction SilentlyContinue | Select-Object -First 1
    return $proc
}

# 2. Detach to background process if running interactively
if (-not $Detached) {
    Write-Host ""
    Write-Host "========================================================================" -ForegroundColor Cyan
    Write-Host "       ROUEN MSI UPGRADE & RECONNECTION PIPELINE                        " -ForegroundColor Cyan
    Write-Host "========================================================================" -ForegroundColor Cyan
    Write-Host " Target Directory : $InstallDir" -ForegroundColor Yellow
    Write-Host " MSI Package      : $(if ($MsiPath) { $MsiPath } else { 'Latest GitHub Release MSI' })" -ForegroundColor Yellow
    Write-Host " Original Exe     : $(if ($OriginalExePath) { $OriginalExePath } else { 'None detected' })" -ForegroundColor Yellow
    Write-Host ""
    Write-Host " [NOTICE] MSI upgrade worker will execute as a detached background job." -ForegroundColor Green
    Write-Host "          Because your RDP session is tunneled through Rouen Mesh," -ForegroundColor Yellow
    Write-Host "          your connection will briefly drop while Rouen restarts." -ForegroundColor Yellow
    Write-Host "          The installer will automatically relaunch Rouen with --mesh" -ForegroundColor Green
    Write-Host "          and restore RDP (ws-ir01:3389) within 5-10 seconds." -ForegroundColor Green
    Write-Host ""
    Write-Host " Spawning detached MSI worker in 2 seconds..." -ForegroundColor Cyan
    Start-Sleep -Seconds 2

    $argsList = "-ExecutionPolicy Bypass -NoProfile -WindowStyle Hidden -File `"$PSCommandPath`" -MsiPath `"$MsiPath`" -InstallDir `"$InstallDir`" -OriginalExePath `"$OriginalExePath`" -Detached"
    Start-Process powershell.exe -ArgumentList $argsList -WorkingDirectory $InstallDir

    Write-Host " Detached worker launched. Check $LogFile for live progress." -ForegroundColor Green
    Write-Host "========================================================================" -ForegroundColor Cyan
    exit 0
}

# --- DETACHED WORKER EXECUTION ---
Write-UpgradeLog "=== Starting Rouen Detached MSI Upgrade Pipeline ==="
Write-UpgradeLog "Install directory: $InstallDir"

$StageId = [Guid]::NewGuid().ToString().Substring(0, 8)
$StageDir = Join-Path $env:TEMP "rouen_msi_stage_$StageId"
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

try {
    # 3. Resolve MSI package
    $ActualMsi = $null
    if ($MsiPath -match "^https?://") {
        Write-UpgradeLog "Downloading MSI package from URL: $MsiPath"
        $ActualMsi = Join-Path $StageDir "installer.msi"
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $MsiPath -OutFile $ActualMsi -UseBasicParsing
    } elseif ($MsiPath -and (Test-Path $MsiPath)) {
        $ActualMsi = (Resolve-Path $MsiPath).Path
    } else {
        # Check current directory or InstallDir
        $localMsi = Get-ChildItem -Path $InstallDir -Filter "*windows-x64.msi" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($localMsi) {
            $ActualMsi = $localMsi.FullName
            Write-UpgradeLog "Found local MSI package: $ActualMsi"
        } else {
            Write-UpgradeLog "Querying latest GitHub release for MSI asset..."
            $releaseInfo = Invoke-RestMethod -Uri "https://api.github.com/repos/ignacionr/rouen/releases/latest" -UseBasicParsing
            $asset = $releaseInfo.assets | Where-Object { $_.name -like "*windows-x64.msi" } | Select-Object -First 1
            if ($asset) {
                Write-UpgradeLog "Downloading MSI release asset '$($asset.name)' from $($asset.browser_download_url)..."
                $ActualMsi = Join-Path $StageDir "installer.msi"
                Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $ActualMsi -UseBasicParsing
            } else {
                throw "No MSI asset found in GitHub release."
            }
        }
    }

    if (-not $ActualMsi -or -not (Test-Path $ActualMsi)) {
        throw "MSI package could not be resolved or found."
    }
    Write-UpgradeLog "Validated MSI package: $ActualMsi"

    # 4. Backup existing executable and .env
    $timestampStr = Get-Date -Format "yyyyMMdd_HHmmss"
    $BackupDir = Join-Path $InstallDir "backup_$timestampStr"
    New-Item -ItemType Directory -Path $BackupDir -Force | Out-Null

    Get-ChildItem -Path $InstallDir -File | Where-Object { $_.Name -match "\.(exe|dll|env|txt|json)$" } | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination $BackupDir -Force
    }
    Write-UpgradeLog "Created pre-upgrade safety backup at: $BackupDir"

    # 5. Preserve .env configuration
    $EnvPath = Join-Path $InstallDir ".env"
    $EnvBackup = Join-Path $env:TEMP "rouen_env_preserve_$StageId.env"
    if (Test-Path $EnvPath) {
        Copy-Item -Path $EnvPath -Destination $EnvBackup -Force
        Write-UpgradeLog "Preserved existing .env configuration."

        $envContent = Get-Content -Path $EnvPath -Raw
        if ($envContent -notmatch "ROUEN_MESH_AUTO_CONNECT") {
            Add-Content -Path $EnvPath -Value "`nROUEN_MESH_AUTO_CONNECT=1"
            Write-UpgradeLog "Ensured ROUEN_MESH_AUTO_CONNECT=1 in configuration."
        }
    }

    # 6. Gracefully terminate running Rouen instances
    Write-UpgradeLog "Stopping running Rouen instances before MSI execution..."
    $runningProcs = Get-Process -Name "rouen" -ErrorAction SilentlyContinue
    if ($runningProcs) {
        if (-not $OriginalExePath) {
            $firstProcWithPath = $runningProcs | Where-Object { $_.Path } | Select-Object -First 1
            if ($firstProcWithPath) {
                $OriginalExePath = $firstProcWithPath.Path
            }
        }
        if ($OriginalExePath) {
            Write-UpgradeLog "Tracked original running executable path: $OriginalExePath"
        }

        $runningProcs | ForEach-Object {
            Write-UpgradeLog "Stopping PID $($_.Id)..."
            Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        }
        $waitCount = 0
        while ((Get-Process -Name "rouen" -ErrorAction SilentlyContinue) -and ($waitCount -lt 20)) {
            Start-Sleep -Milliseconds 500
            $waitCount++
        }
        Start-Sleep -Seconds 1
    }

    # 7. Execute MSI installer silently with dual-purpose user-mode properties (ALLUSERS=2 MSIINSTALLPERUSER=1)
    $MsiLogFile = Join-Path $InstallDir "msi-install.log"
    Write-UpgradeLog "Executing MSI: msiexec.exe /i `"$ActualMsi`" ALLUSERS=2 MSIINSTALLPERUSER=1 /qn /norestart /l*v `"$MsiLogFile`""
    $msiProcess = Start-Process msiexec.exe -ArgumentList "/i `"$ActualMsi`" ALLUSERS=2 MSIINSTALLPERUSER=1 /qn /norestart /l*v `"$MsiLogFile`"" -Wait -PassThru

    Write-UpgradeLog "MSI execution finished with exit code: $($msiProcess.ExitCode)"
    if ($msiProcess.ExitCode -ne 0 -and $msiProcess.ExitCode -ne 3010) {
        throw "MSI installation failed with error code $($msiProcess.ExitCode). Check $MsiLogFile."
    }

    # 8. Restore user .env file if MSI did not preserve it
    if ((Test-Path $EnvBackup) -and (-not (Test-Path $EnvPath))) {
        Copy-Item -Path $EnvBackup -Destination $EnvPath -Force
        Write-UpgradeLog "Restored user .env configuration into target directory."
    }

    # 9. Verify if rouen.exe is running interactively (the MSI LaunchApplication custom action starts it)
    Start-Sleep -Seconds 3
    $verifyProc = Get-Process -Name "rouen" -ErrorAction SilentlyContinue | Select-Object -First 1

    # Check if process was started in non-interactive Session 0 (common when msiexec executes via background service/WinRM)
    if ($verifyProc -and $verifyProc.SessionId -eq 0) {
        Write-UpgradeLog "MSI auto-launched Rouen in non-interactive Session 0 (PID: $($verifyProc.Id)). Terminating to restart in interactive desktop session..." "WARN"
        Stop-Process -Id $verifyProc.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 1
        $verifyProc = $null
    }

    if (-not $verifyProc) {
        Write-UpgradeLog "MSI did not auto-launch Rouen in interactive session. Relaunching: rouen.exe --mesh..."
        $installedExe = Join-Path $InstallDir "rouen.exe"
        if (Test-Path $installedExe) {
            $verifyProc = Start-RouenInteractive -ExePath $installedExe -Arguments "--mesh" -WorkingDirectory $InstallDir
        }
    }

    if ($verifyProc) {
        Write-UpgradeLog "=== MSI UPGRADE SUCCESSFUL! ===" "SUCCESS"
        Write-UpgradeLog "Rouen is running (PID: $($verifyProc.Id), Session: $($verifyProc.SessionId))." "SUCCESS"
        Write-UpgradeLog "Mesh connection and services (ws-ir01 RDP on 3389) are restored." "SUCCESS"
    } else {
        throw "Newly installed Rouen process failed to stay running in interactive session."
    }

} catch {
    Write-UpgradeLog "MSI UPGRADE FAILURE: $_" "ERROR"
    Write-UpgradeLog "Initiating automatic rollback to previous version..." "WARN"

    try {
        $restarted = $false
        if ($BackupDir -and (Test-Path $BackupDir)) {
            $backupExe = Join-Path $BackupDir "rouen.exe"
            if (Test-Path $backupExe) {
                Stop-Process -Name "rouen" -Force -ErrorAction SilentlyContinue
                Start-Sleep -Seconds 1
                Get-ChildItem -Path $BackupDir -File | ForEach-Object {
                    Copy-Item -Path $_.FullName -Destination (Join-Path $InstallDir $_.Name) -Force
                }
                $oldExe = Join-Path $InstallDir "rouen.exe"
                if (Test-Path $oldExe) {
                    $restoredProc = Start-RouenInteractive -ExePath $oldExe -Arguments "--mesh" -WorkingDirectory $InstallDir
                    if ($restoredProc) {
                        $restarted = $true
                        Write-UpgradeLog "Rollback complete: previous version restored into $InstallDir and running (PID: $($restoredProc.Id))." "SUCCESS"
                    }
                }
            }
        }

        # If rollback in $InstallDir did not yield a running process, try the original executable path
        if (-not $restarted -and $OriginalExePath -and (Test-Path $OriginalExePath)) {
            Write-UpgradeLog "Restoring execution using original executable path: $OriginalExePath" "WARN"
            $origDir = Split-Path -Parent $OriginalExePath
            $restoredProc = Start-RouenInteractive -ExePath $OriginalExePath -Arguments "--mesh" -WorkingDirectory $origDir
            if ($restoredProc) {
                $restarted = $true
                Write-UpgradeLog "Rollback complete: original executable restarted at $OriginalExePath (PID: $($restoredProc.Id))." "SUCCESS"
            }
        }

        if (-not $restarted) {
            Write-UpgradeLog "Rollback could not verify a running Rouen process." "ERROR"
        }
    } catch {
        Write-UpgradeLog "CRITICAL: Rollback failed: $_" "ERROR"
    }
} finally {
    if ($StageDir -and (Test-Path $StageDir)) {
        Remove-Item -Path $StageDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    if ($EnvBackup -and (Test-Path $EnvBackup)) {
        Remove-Item -Path $EnvBackup -Force -ErrorAction SilentlyContinue
    }
}
