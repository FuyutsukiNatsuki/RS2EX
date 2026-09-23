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
#
#   The layout itself is part of the world too. At start-up the program loads
#   Layout\<LastFile> named in Config.txt, not the Undo copy, so a snapshot of
#   Config.txt and Undo alone is only pinned for as long as nobody saves over
#   that layout. That happened: the layout behind the v0.1.1 fixture was saved
#   over during later manual testing, and the same snapshot then started in a
#   different scene. A snapshot therefore carries its layout in Layout\, and
#   restore puts it back. Give fixture layouts their own names (RS2EX_Fixture_*)
#   so a restore never writes over a layout somebody is working on.

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

#   The layout Config.txt tells the program to load at start-up.
function Get-LastFile([string]$config) {
    if (-not (Test-Path $config)) { return $null }
    $text = [System.IO.File]::ReadAllText($config, [System.Text.Encoding]::GetEncoding(932))
    $m = [regex]::Match($text, 'LastFile\s*=\s*"([^"]+)"')
    if ($m.Success) { return $m.Groups[1].Value }
    return $null
}

if ($Action -eq "save") {
    if (Test-Path $Snapshot) { Remove-Item $Snapshot -Recurse -Force }
    $null = New-Item -ItemType Directory -Path $Snapshot

    $last = Get-LastFile (Join-Path $RunDir "Config.txt")
    if ($last) {
        $src = Join-Path (Join-Path $RunDir "Layout") $last
        if (Test-Path $src) {
            $null = New-Item -ItemType Directory -Path (Join-Path $Snapshot "Layout")
            Copy-Item $src (Join-Path (Join-Path $Snapshot "Layout") $last) -Force
            Write-Host "saved Layout\$last"
        }
    }

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

    #   Layouts are copied in, never mirrored: the folder also holds layouts
    #   that belong to whoever uses this tree.
    $layouts = Join-Path $Snapshot "Layout"
    if (Test-Path $layouts) {
        foreach ($file in Get-ChildItem $layouts -File) {
            Copy-Item $file.FullName (Join-Path (Join-Path $RunDir "Layout") $file.Name) -Force
            Write-Host "restored Layout\$($file.Name)"
        }
    }

    $last = Get-LastFile (Join-Path $RunDir "Config.txt")
    if ($last -and -not (Test-Path (Join-Path (Join-Path $RunDir "Layout") $last))) {
        throw "Config.txt starts from Layout\$last, which is not there"
    }
}
