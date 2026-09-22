#   Launch a RailSim II build and screenshot it reproducibly.
#
#   CopyFromScreen reads the screen rather than the window, and
#   SetForegroundWindow is refused to a background process, so a naive capture
#   photographs whatever happens to be on top.  Moving the window to empty
#   desktop as topmost needs no activation rights, and a move does not change
#   the client size, so it does not trigger the resize/reset path.
#
#   The program draws its own cursor and highlights whatever is under it, so
#   the cursor is parked at a fixed spot too.  Without that, two runs differ
#   wherever the mouse happened to be.
#
#   Config.txt normally has FullScreen = yes, so -win is passed by default;
#   without it there is no window handle to find.

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Out,

    #   Seconds to let the layout load and the scene settle before capturing.
    [int]$Settle = 14,

    #   Extra command-line arguments, e.g. "-fixture" or "-dx12".
    [string]$ExtraArgs = "",

    #   Wait for this text to appear in the program's log before capturing,
    #   instead of trusting the settle time. A fixed wait caught the loading
    #   screen about one run in four when the machine was busy, and a capture
    #   of the loading screen looks exactly like a renderer that lost the
    #   scene. Needs -dbf in ExtraArgs, which is what writes the log.
    [string]$WaitForLog = "",

    [int]$WaitForLogSeconds = 120,

    #   Pass -Fullscreen to omit -win and capture the primary screen instead.
    [switch]$Fullscreen,

    #   Somewhere the window can sit without overlapping anything.
    [int]$X = 660,
    [int]$Y = 30,

    #   Do not send SetWindowPos to a smoke test that is deliberately holding
    #   its final frame on the UI thread. The synchronous window message would
    #   wait until the hold ends and lose the frame before capture.
    [switch]$NoMove,

    #   Short-lived smoke tests may finish before the normal scene-oriented
    #   settle delays expire. Defaults preserve the established fixture path.
    [int]$PositionSettleMilliseconds = 700,
    [int]$CursorSettleMilliseconds = 300,

    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$sig = @'
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool SetWindowPos(
    IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
[DllImport("user32.dll")] public static extern int PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
'@

$api = Add-Type -MemberDefinition $sig -Name Shot -Namespace RS2EX -PassThru |
    Select-Object -First 1

if (-not (Test-Path $Exe)) { throw "no such executable: $Exe" }

$exePath = (Resolve-Path $Exe).Path
$workDir = Split-Path -Parent $exePath
$procName = [System.IO.Path]::GetFileNameWithoutExtension($exePath)
$log = Join-Path $workDir "debug.txt"

$argList = @()
if (-not $Fullscreen) { $argList += "-win" }
if ($ExtraArgs) { $argList += ($ExtraArgs -split '\s+' | Where-Object { $_ }) }

Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600

$logStartBytes = 0
if ($WaitForLog -and (Test-Path $log)) {
    # Record the boundary only after the old process is gone. It may append
    # shutdown lines after Stop-Process was requested, and those lines belong
    # to the previous run, not the process launched below.
    $logStartBytes = (Get-Item $log).Length
}

if ($argList.Count) {
    $proc = Start-Process -FilePath $exePath -ArgumentList $argList `
        -WorkingDirectory $workDir -PassThru
} else {
    $proc = Start-Process -FilePath $exePath -WorkingDirectory $workDir -PassThru
}

$windowHandle = [IntPtr]::Zero

try {
    if ($Fullscreen) {
        Start-Sleep -Seconds $Settle
        $proc.Refresh()
        if ($proc.HasExited) { throw "the program exited during start-up" }
        $windowHandle = $proc.MainWindowHandle
        if ($windowHandle -eq 0) { throw "no fullscreen window appeared" }

        # A process started from a background test runner cannot activate its
        # own window reliably.  Make it topmost without changing its size or
        # focus so CopyFromScreen captures the borderless output, not Codex.
        # HWND_TOPMOST, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE.
        $null = $api::SetWindowPos($windowHandle, [IntPtr](-1), 0, 0, 0, 0, 0x0013)
        Start-Sleep -Milliseconds $PositionSettleMilliseconds

        $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $left = $b.X; $top = $b.Y; $w = $b.Width; $h = $b.Height
    } else {
        #   Wait for a window handle rather than guessing how long start-up takes.
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            $proc.Refresh()
            if ($proc.HasExited) { throw "the program exited during start-up" }
            if ($proc.MainWindowHandle -ne 0) {
                $windowHandle = $proc.MainWindowHandle
                break
            }
            Start-Sleep -Milliseconds 50
        }
        if ($windowHandle -eq 0) { throw "no window appeared within ${TimeoutSeconds}s" }

        if ($WaitForLog) {
            $deadline = (Get-Date).AddSeconds($WaitForLogSeconds)
            $seen = $false

            while ((Get-Date) -lt $deadline) {
                $proc.Refresh()
                if ($proc.HasExited) { throw "the program exited before logging '$WaitForLog'" }
                if (Test-Path $log) {
                    try {
                        $stream = [System.IO.File]::Open(
                            $log,
                            [System.IO.FileMode]::Open,
                            [System.IO.FileAccess]::Read,
                            [System.IO.FileShare]::ReadWrite)
                        try {
                            $start = [Math]::Min($logStartBytes, $stream.Length)
                            if ($stream.Length -gt $start) {
                                $null = $stream.Seek($start, [System.IO.SeekOrigin]::Begin)
                                $bytes = New-Object byte[] ($stream.Length - $start)
                                $read = $stream.Read($bytes, 0, $bytes.Length)

                                # The marker is ASCII. Reading only bytes appended by
                                # this process prevents an earlier run from satisfying
                                # the wait. FileShare.ReadWrite is essential: Debug()
                                # appends one line at a time and must not be blocked.
                                $newLog = [System.Text.Encoding]::ASCII.GetString(
                                    $bytes, 0, $read)
                                if ($newLog.Contains($WaitForLog)) {
                                    $proc.Refresh()
                                    Write-Host ("marker '{0}' seen ({1} appended bytes, exited={2})" -f `
                                        $WaitForLog, $read, $proc.HasExited)
                                    $seen = $true
                                    break
                                }
                            }
                        } finally {
                            $stream.Dispose()
                        }
                    } catch [System.IO.IOException] {
                        # Debug() opens and closes the file for every line. If this
                        # poll overlaps a write, retry on the next interval.
                    }
                }
                Start-Sleep -Milliseconds 200
            }
            if (-not $seen) { throw "'$WaitForLog' did not appear within ${WaitForLogSeconds}s" }
        }

        Start-Sleep -Seconds $Settle

        #   RailSim replaces its loading window while the real scene starts.
        #   Keep the early handle for short smoke tests, but prefer the current
        #   main window once loading has completed.
        $proc.Refresh()
        if ($proc.MainWindowHandle -ne 0) {
            $windowHandle = $proc.MainWindowHandle
        }

        #   HWND_TOPMOST, SWP_NOSIZE | SWP_NOACTIVATE.
        $moved = $false
        if (-not $NoMove) {
            $moved = $api::SetWindowPos($windowHandle, [IntPtr](-1), $X, $Y, 0, 0, 0x0011)
        }
        Start-Sleep -Milliseconds $PositionSettleMilliseconds

        $rc = New-Object RS2EX.Shot+RECT
        $gotRect = $api::GetWindowRect($windowHandle, [ref]$rc)
        $left = $rc.L; $top = $rc.T; $w = $rc.R - $rc.L; $h = $rc.B - $rc.T

        $proc.Refresh()
        Write-Host ("capture hwnd={0} move={1} rect={2} exited={3}" -f `
            $windowHandle, $moved, $gotRect, $proc.HasExited)

        $null = $api::SetCursorPos(($left + 300), ($top + 300))
        Start-Sleep -Milliseconds $CursorSettleMilliseconds
    }

    if ($w -le 0 -or $h -le 0) { throw "window has no area ($w x $h)" }

    $dir = Split-Path -Parent $Out
    if ($dir -and -not (Test-Path $dir)) { $null = New-Item -ItemType Directory -Path $dir }

    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.CopyFromScreen($left, $top, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    $gfx.Dispose()
    $bmp.Dispose()

    Write-Host ("saved {0} ({1}x{2})" -f $Out, $w, $h)
}
finally {
    $proc.Refresh()
    if (-not $proc.HasExited) {
        #   WM_CLOSE, so the program shuts down through its own path.
        $handle = if ($windowHandle -ne 0) { $windowHandle } else { $proc.MainWindowHandle }
        $null = $api::PostMessage($handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
        $deadline = (Get-Date).AddSeconds(20)
        while ((Get-Date) -lt $deadline -and -not $proc.HasExited) {
            Start-Sleep -Milliseconds 400
            $proc.Refresh()
        }
        if (-not $proc.HasExited) { $proc | Stop-Process -Force }
    }
}
