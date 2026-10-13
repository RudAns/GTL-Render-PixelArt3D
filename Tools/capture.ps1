# Run the scripted capture only. Usage: powershell -File Tools/capture.ps1 [-Only 05,06] [-Build]
param([string]$Only = "", [switch]$Build, [int]$ResX = 1600, [int]$ResY = 900)
$p = Split-Path -Parent $PSScriptRoot
$ue = Join-Path (& "$PSScriptRoot\find_ue.ps1") "Engine"
$proj = "$p\PixelArt3D.uproject"
if ($Build) {
    $b = & "$ue\Build\BatchFiles\Build.bat" PixelArt3DEditor Win64 Development -Project="$proj" -WaitMutex 2>&1
    $b | Select-String -Pattern "error C|error LNK|Result:" | ForEach-Object { $_.Line }
    if (-not ($b | Select-String -Pattern "Result: Succeeded")) { "BUILD FAILED - stopping"; exit 1 }
}
$out = "$p\Saved\Capture"
New-Item -ItemType Directory -Force $out | Out-Null
Get-ChildItem $out -Filter *.png | Remove-Item -Force
$args = @("`"$proj`"", "-game", "-windowed", "-ResX=$ResX", "-ResY=$ResY", "-PixelCapture=$out", "-RenderOffScreen", "-ForceRes", "-ExecCmds=`"r.SetRes ${ResX}x${ResY}w`"", "-unattended", "-nosound", "-nosplash", "-log=capture.log")
if ($Only) { $args += "-PixelCaptureOnly=$Only,zz" }
$proc = Start-Process -FilePath "$ue\Binaries\Win64\UnrealEditor.exe" -ArgumentList $args -PassThru
if (-not $proc.WaitForExit(480000)) { $proc.Kill(); "capture timeout" }
Get-ChildItem $out -Filter *.png | ForEach-Object { $_.Name }
Select-String -Path "$p\Saved\Logs\capture.log" -Pattern "Capture .*flicker|PixelArt readback|Flicker sample|Error: .*Material|Shader.*error" | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
