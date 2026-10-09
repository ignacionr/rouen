<#
.SYNOPSIS
    Rouen Adaptive Process: Inbox Status Dashboard
    Emits compact single-line Adaptive Card JSON to stdout and handles Action.Submit on stdin.
#>

param(
    [string]$InboxDir = "$HOME/rouen/inbox"
)

if (-not (Test-Path $InboxDir)) {
    if (Test-Path "$HOME/src/rouen/inbox") {
        $InboxDir = "$HOME/src/rouen/inbox"
    } elseif (Test-Path "./inbox") {
        $InboxDir = "./inbox"
    }
}

function Emit-AdaptiveCard {
    $doneCount = 0
    $pendingCount = 0
    $inProgressCount = 0

    $doneItems = @()
    $pendingItems = @()
    $inProgressItems = @()

    if (Test-Path $InboxDir) {
        $files = Get-ChildItem -Path $InboxDir -Filter "*.md"
        foreach ($file in $files) {
            if ($file.Name -eq "README.md") { continue }

            $title = $file.Name
            $content = Get-Content -Path $file.FullName -Raw -ErrorAction SilentlyContinue
            if ($content -match "(?m)^#\s+(.+)$") {
                $title = $matches[1].Trim()
            }

            if ($file.Name -like "done_*") {
                $doneCount++
                if ($doneItems.Count -lt 8) {
                    $doneItems += @{ type = "TextBlock"; text = "- $title"; wrap = $true; isSubtle = $true; size = "small" }
                }
            } elseif ($content -match "(?i)status:\s*in[ _-]progress") {
                $inProgressCount++
                if ($inProgressItems.Count -lt 8) {
                    $inProgressItems += @{ type = "TextBlock"; text = "- $title"; wrap = $true; color = "accent"; weight = "bolder"; size = "small" }
                }
            } else {
                $pendingCount++
                if ($pendingItems.Count -lt 8) {
                    $pendingItems += @{ type = "TextBlock"; text = "- $title"; wrap = $true; size = "small" }
                }
            }
        }
    }

    $nowStr = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")

    $body = @(
        @{ type = "TextBlock"; text = "Rouen Inbox Status"; size = "medium"; weight = "bolder"; color = "accent" },
        @{ type = "TextBlock"; text = "Folder: $InboxDir | Updated: $nowStr"; size = "small"; isSubtle = $true; spacing = "none" },
        @{
            type = "FactSet"
            facts = @(
                @{ title = "In Progress:"; value = "$inProgressCount" },
                @{ title = "Pending:"; value = "$pendingCount" },
                @{ title = "Completed:"; value = "$doneCount" }
            )
        },
        @{ type = "TextBlock"; text = "In Progress ($inProgressCount)"; weight = "bolder"; color = "warning" }
    )

    if ($inProgressCount -gt 0) {
        $body += $inProgressItems
    } else {
        $body += @{ type = "TextBlock"; text = "None currently in progress."; isSubtle = $true; size = "small" }
    }

    $body += @{ type = "TextBlock"; text = "Pending ($pendingCount)"; weight = "bolder"; color = "accent" }
    if ($pendingCount -gt 0) {
        $body += $pendingItems
    } else {
        $body += @{ type = "TextBlock"; text = "No pending items."; isSubtle = $true; size = "small" }
    }

    $body += @{ type = "TextBlock"; text = "Completed ($doneCount)"; weight = "bolder"; color = "good" }
    if ($doneCount -gt 0) {
        $body += $doneItems
    }

    $card = @{
        type = "AdaptiveCard"
        version = "1.5"
        refreshIntervalMs = 5000
        body = $body
        actions = @(
            @{
                type = "Action.Submit"
                title = "Refresh Now"
                data = @{ action = "refresh" }
            }
        )
    }

    $json = $card | ConvertTo-Json -Depth 10 -Compress
    [Console]::Out.WriteLine($json)
}

Emit-AdaptiveCard

# Main update / stdin loop
while ($true) {
    Start-Sleep -Seconds 5
    Emit-AdaptiveCard
}
