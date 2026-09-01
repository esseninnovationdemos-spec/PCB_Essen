# Renders one tour video for one level.
#
#   render_tour.ps1 /Game/level5 Butchery_Exterior_60fps
#
# The camera path comes from shots.json, cutting between viewpoints; see the
# -Tour branch in FactoryRenderCommandlet. MP4 out, 60 fps, 1080p.

param(
    [Parameter(Mandatory = $true)][string]$Level,
    [Parameter(Mandatory = $true)][string]$Name,
    [int]$Width = 1920,
    [int]$Height = 1080,
    [int]$Fps = 60,
    [int]$TimeoutMinutes = 200
)

$UE   = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$UEG  = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$PROJ = "C:\Users\Metaverse\Documents\PCB_Essen\AutoMotion_PCB.uproject"
$OUT  = "C:\Users\Metaverse\Documents\PCB_Essen\Saved\Renders"
$SEQ  = "/Game/Cinematics/LS_PlantFlythrough.LS_PlantFlythrough"
$CFG  = "/Game/Cinematics/MRQ_PlantFlythrough.MRQ_PlantFlythrough"

Get-Process UnrealEditor*, CrashReportClient* -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

Write-Output "authoring $Name over $Level"
& $UE $PROJ -run=FactoryRender "-Level=$Level" -Tour "-Fps=$Fps" `
    "-Width=$Width" "-Height=$Height" "-Shot=$Name" -unattended -nosound 2>&1 |
    Select-String "tour:|Wrote /Game" | ForEach-Object { $_.ToString().Trim() }

$target = Join-Path $OUT "$Name.mp4"
if (Test-Path $target) { Remove-Item $target -Force }

Write-Output "rendering $Name"
$start = Get-Date
$p = Start-Process -FilePath $UEG -PassThru -NoNewWindow -ArgumentList @(
    $PROJ, $Level, "-game", "-unattended", "-nosound", "-windowed",
    "-ResX=1280", "-ResY=720", "-LevelSequence=$SEQ", "-MoviePipelineConfig=$CFG")

# MRQ writes the container as it goes and finalises at the end, so a file that
# exists is not a file that is finished. Wait for the process to exit instead.
$deadline = (Get-Date).AddMinutes($TimeoutMinutes)
while (-not $p.HasExited -and (Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 20
}
if (-not $p.HasExited) {
    Write-Output "TIMEOUT after $TimeoutMinutes min"
    $p.Kill()
}
Get-Process UnrealEditor*, CrashReportClient* -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue

$mins = [math]::Round(((Get-Date) - $start).TotalMinutes, 1)
if (Test-Path $target) {
    $mb = [math]::Round((Get-Item $target).Length / 1MB, 1)
    Write-Output "done: $Name.mp4  $mb MB  in $mins min"
} else {
    Write-Output "FAILED: no $Name.mp4 after $mins min"
    Get-ChildItem $OUT -Filter "*.mp4" | Select-Object Name, Length | Out-String
}
