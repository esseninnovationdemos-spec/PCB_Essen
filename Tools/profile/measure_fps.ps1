# Measures a level's frame rate, for A/B testing a rendering setting.
#
#   .\measure_fps.ps1 -Width 2560 -Height 1440 -Tag baseline -MaxFps 195
#   .\measure_fps.ps1 -Width 2560 -Height 1440 -Tag nolumen  -MaxFps 195 `
#       -Extra "r.DynamicGlobalIlluminationMethod 0,r.ReflectionMethod 0"
#
# There is no profiler in the loop. The CSV profiler writes nothing in this
# build and ProfileGPU does not fire from -ExecCmds, but every log line is
# stamped with the frame counter and the sim prints a line on a fixed
# five-second wall clock, so frames between two of those lines over the seconds
# between them is a frame rate that needs no tooling at all.
#
# Two traps, both of which produce confident wrong answers rather than errors:
#
#   -ExecCmds must be quoted. Passed as an element of Start-Process's
#   -ArgumentList array, PowerShell leaves the value bare, so the engine reads
#   -ExecCmds=r.setres and treats everything after the first space as separate
#   arguments. Nothing errors and no command runs -- so every A/B measures the
#   same unmodified build twice, which reads exactly like "this setting makes no
#   difference to anything". Verify with a cap: -MaxFps 60 must measure 60.
#
#   The frame counter in the log wraps at 1000. Over a five-second window that
#   makes anything at or above 200 fps alias onto a lower number: a true 359 fps
#   reads as 159. Keep MaxFps under 200 so the count cannot wrap, and treat a
#   result that sits at the cap as "faster than the cap", not as a measurement.

param(
    [Parameter(Mandatory = $true)][int]$Width,
    [Parameter(Mandatory = $true)][int]$Height,
    [string]$Level = "/Game/level4",
    [string]$Tag = "run",
    [string]$Extra = "",
    [int]$Seconds = 55,
    [int]$MaxFps = 180
)

$UEG = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$P   = "C:\Users\Metaverse\Documents\PCB_Essen\AutoMotion_PCB.uproject"
$log = "C:\Users\METAVE~1\AppData\Local\Temp\claude\C--Users-Metaverse-Documents-PCB-Essen\7f58fa3d-fb86-45d7-9a28-754c0144bf7b\scratchpad\fps_$Tag.log"

$cmds = "r.setres $($Width)x$($Height)w,t.MaxFPS $MaxFps,r.VSync 0"
if ($Extra) { $cmds = "$cmds,$Extra" }
# One string, with -ExecCmds quoted. Passed as an array element PowerShell
# leaves the value unquoted, so the engine reads -ExecCmds=r.setres and takes
# everything after the first space as separate arguments. Nothing errors and
# nothing runs: every A/B then measures the same unmodified build twice, which
# looks exactly like "this setting makes no difference".
$line = '"{0}" {1} -game -windowed -nosound -ExecCmds="{2}" -abslog="{3}"' -f $P, $Level, $cmds, $log

$p = Start-Process -FilePath $UEG -PassThru -NoNewWindow -ArgumentList $line
Start-Sleep -Seconds $Seconds
if (-not $p.HasExited) { $p.Kill() }
Get-Process UnrealEditor*, CrashReportClient* -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

# Pull (timestamp, frame) off the five-second heartbeat the sim already prints.
$rows = @()
foreach ($line in Get-Content $log) {
    if ($line -match '^\[[\d.]+-(\d\d)\.(\d\d)\.(\d\d):(\d+)\]\[\s*(\d+)\]LogFactorySim: New material') {
        $t = [double]$matches[1] * 3600 + [double]$matches[2] * 60 +
             [double]$matches[3] + [double]$matches[4] / 1000.0
        $rows += [pscustomobject]@{ T = $t; F = [int]$matches[5] }
    }
}

$fps = @()
for ($i = 1; $i -lt $rows.Count; $i++) {
    $dt = $rows[$i].T - $rows[$i - 1].T
    $df = $rows[$i].F - $rows[$i - 1].F
    if ($df -lt 0) { $df += 1000 }          # the counter is printed modulo 1000
    if ($dt -gt 0.5 -and $df -gt 0) { $fps += $df / $dt }
}

if ($fps.Count -ge 3) {
    # Drop the first sample: it covers the frames while the level was still
    # warming its shaders and streaming, which is not what anyone is looking at.
    $stable = $fps[1..($fps.Count - 1)]
    $mean = ($stable | Measure-Object -Average).Average
    "{0,-22} {1}x{2}  {3,6:N1} fps  ({4,5:N2} ms)  n={5}" -f
        $Tag, $Width, $Height, $mean, (1000.0 / $mean), $stable.Count
} else {
    "{0,-22} {1}x{2}  no usable samples" -f $Tag, $Width, $Height
}


