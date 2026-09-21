#   Pin the world the fixture starts from.
#
#   -fixture makes a run reproducible, but only from wherever the program
#   happens to resume. The program saves its state on exit and picks it up
#   again next time, so a long run - or any run at all - moves the starting
#   point. Two captures taken a day apart differed in 99.6% of pixels for that
#   reason alone, with nothing in the renderer having changed.
#
#   So the state is snapshotted once and restored before each capture. Then
#   "-fixture is deterministic" means what it sounds like: the same world, the
#   same frame, every time.
#
#   The snapshot lives outside the repository. It is someone's layout, it is
#   several megabytes, and it is not source.

param(
    [Parameter(Mandatory = $true)][ValidateSet("save", "restore")][string]$Action,

    #   Where the snapshot is kept.
    [Parameter(Mandatory = $true)][string]$Snapshot,

    [string]$RunDir = "C:\RS2EX\run\RailSim2"
)

$ErrorActionPreference = "Stop"

#   What the program rewrites between runs. Config.txt holds the settings and
#   the state it resumes from; Undo holds the autosaved world.
$items = @("Config.txt", "Undo")

#   Nothing may be running. The program writes its state while shutting down,
#   so an instance still on its way out will overwrite a restore that has
#   already happened - which is a race that produces one wrong capture in a run
#   of good ones, and looks exactly like the change under test.
function Wait-ForExit {
    foreach ($name in @("RailSim2_Release_vc2010", "RailSim2_Debug_vc2010", "RailSim2")) {
        $procs = Get-Process $name -ErrorAction SilentlyContinue
        if (-not $procs) { continue }

        $procs | Stop-Process -Force -ErrorAction SilentlyContinue

        $deadline = (Get-Date).AddSeconds(15)
        while ((Get-Date) -lt $deadline) {
            if (-not (Get-Process $name -ErrorAction SilentlyContinue)) { break }
            Start-Sleep -Milliseconds 200
        }
        Write-Host "stopped $name"
    }

    #   Give the file system a moment to settle after the handles close.
    Start-Sleep -Milliseconds 400
}

Wait-ForExit

if ($Action -eq "save") {
    if (Test-Path $Snapshot) { Remove-Item $Snapshot -Recurse -Force }
    $null = New-Item -ItemType Directory -Path $Snapshot

    foreach ($item in $items) {
        $src = Join-Path $RunDir $item
        if (Test-Path $src) {
            Copy-Item $src (Join-Path $Snapshot $item) -Recurse -Force
            Write-Host "saved $item"
        } else {
            Write-Host "skipped $item (not present)"
        }
    }
} else {
    if (-not (Test-Path $Snapshot)) { throw "no snapshot at $Snapshot" }

    foreach ($item in $items) {
        $src = Join-Path $Snapshot $item
        if (-not (Test-Path $src)) { continue }

        $dst = Join-Path $RunDir $item
        if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
        Copy-Item $src $dst -Recurse -Force
        Write-Host "restored $item"
    }
}
