# Renders every shot in shots.json.
#
# One engine launch per shot, because MRQ warms Lumen up per job and a camera
# that teleports between frames carries the previous view's indirect lighting
# into the next one. Startup dominates, so the script waits for the output file
# to appear rather than sleeping a fixed time -- that alone roughly halves a
# 28-shot batch.

$ErrorActionPreference = "Continue"

$UE   = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$UEG  = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$PROJ = "C:\Users\Metaverse\Documents\PCB_Essen\AutoMotion_PCB.uproject"
$OUT  = "C:\Users\Metaverse\Documents\PCB_Essen\Saved\Renders"
$DEST = "C:\Users\Metaverse\Documents\PCB_Essen\Tools\butchery\showcase"
$SEQ  = "/Game/Cinematics/LS_PlantFlythrough.LS_PlantFlythrough"
$CFG  = "/Game/Cinematics/MRQ_PlantFlythrough.MRQ_PlantFlythrough"

New-Item -ItemType Directory -Force -Path $DEST | Out-Null

$shots = Get-Content "C:\Users\Metaverse\Documents\PCB_Essen\Tools\butchery\shots.json" -Raw | ConvertFrom-Json
$total = $shots.Count
$index = 0
$ok = 0
$failed = @()

foreach ($s in $shots) {
    $index++
    $name = $s.name
    $from = "{0},{1},{2}" -f $s.from[0], $s.from[1], $s.from[2]
    $look = "{0},{1},{2}" -f $s.look[0], $s.look[1], $s.look[2]

    Write-Output ("[{0,2}/{1}] {2}  {3}" -f $index, $total, $name, $s.note)

    Get-ChildItem $OUT -Filter "$name.*.png" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue

    # Author the one-camera sequence for this viewpoint.
    & $UE $PROJ -run=FactoryRender "-Level=$($s.level)" "-From=$from" "-LookAt=$look" `
        -Width=2560 -Height=1440 -Samples=8 "-Shot=$name" -unattended -nosound 2>&1 | Out-Null

    $p = Start-Process -FilePath $UEG -PassThru -NoNewWindow -ArgumentList @(
        $PROJ, $s.level, "-game", "-unattended", "-nosound", "-windowed",
        "-ResX=1280", "-ResY=720", "-LevelSequence=$SEQ", "-MoviePipelineConfig=$CFG")

    # Poll for the last frame rather than sleeping a fixed time.
    $target = Join-Path $OUT "$name.0002.png"
    $waited = 0
    while ($waited -lt 300) {
        Start-Sleep -Seconds 5
        $waited += 5
        if (Test-Path $target) {
            Start-Sleep -Seconds 4      # let the write finish
            break
        }
        if ($p.HasExited) { break }
    }

    if (-not $p.HasExited) { $p.Kill() }
    Get-Process UnrealEditor*, CrashReportClient* -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2

    $frames = Get-ChildItem $OUT -Filter "$name.*.png" -ErrorAction SilentlyContinue |
        Sort-Object Name
    if ($frames) {
        Copy-Item $frames[-1].FullName (Join-Path $DEST "$name.png") -Force
        $frames | Remove-Item -Force -ErrorAction SilentlyContinue
        $ok++
        Write-Output ("       kept {0} ({1:N0} kB) after {2}s" -f $name, ($frames[-1].Length / 1KB), $waited)
    } else {
        $failed += $name
        Write-Output ("       FAILED {0} after {1}s" -f $name, $waited)
    }
}

Write-Output ""
Write-Output ("Showcase: {0}/{1} rendered into {2}" -f $ok, $total, $DEST)
if ($failed.Count -gt 0) { Write-Output ("Failed: " + ($failed -join ", ")) }
