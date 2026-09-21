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

    #   Pass -Fullscreen to omit -win and capture the primary screen instead.
    [switch]$Fullscreen,

    #   Somewhere the window can sit without overlapping anything.
    [int]$X = 660,
    [int]$Y = 30,

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

$argList = @()
if (-not $Fullscreen) { $argList += "-win" }
if ($ExtraArgs) { $argList += ($ExtraArgs -split '\s+' | Where-Object { $_ }) }

Get-Process $procName -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 600

if ($argList.Count) {
    $proc = Start-Process -FilePath $exePath -ArgumentList $argList `
        -WorkingDirectory $workDir -PassThru
} else {
    $proc = Start-Process -FilePath $exePath -WorkingDirectory $workDir -PassThru
}

try {
    if ($Fullscreen) {
        Start-Sleep -Seconds $Settle
        $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $left = $b.X; $top = $b.Y; $w = $b.Width; $h = $b.Height
    } else {
        #   Wait for a window handle rather than guessing how long start-up takes.
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ((Get-Date) -lt $deadline) {
            $proc.Refresh()
            if ($proc.HasExited) { throw "the program exited during start-up" }
            if ($proc.MainWindowHandle -ne 0) { break }
            Start-Sleep -Milliseconds 50
        }
        if ($proc.MainWindowHandle -eq 0) { throw "no window appeared within ${TimeoutSeconds}s" }

        Start-Sleep -Seconds $Settle

        #   HWND_TOPMOST, SWP_NOSIZE | SWP_NOACTIVATE.
        $null = $api::SetWindowPos($proc.MainWindowHandle, [IntPtr](-1), $X, $Y, 0, 0, 0x0011)
        Start-Sleep -Milliseconds 700

        $rc = New-Object RS2EX.Shot+RECT
        $null = $api::GetWindowRect($proc.MainWindowHandle, [ref]$rc)
        $left = $rc.L; $top = $rc.T; $w = $rc.R - $rc.L; $h = $rc.B - $rc.T

        $null = $api::SetCursorPos(($left + 300), ($top + 300))
        Start-Sleep -Milliseconds 300
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
        $null = $api::PostMessage($proc.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
        $deadline = (Get-Date).AddSeconds(20)
        while ((Get-Date) -lt $deadline -and -not $proc.HasExited) {
            Start-Sleep -Milliseconds 400
            $proc.Refresh()
        }
        if (-not $proc.HasExited) { $proc | Stop-Process -Force }
    }
}
