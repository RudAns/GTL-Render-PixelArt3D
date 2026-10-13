# Build -> (optionally) rebuild assets -> run the scripted capture. Usage:
#   powershell -File Tools/iterate.ps1 [-Assets] [-NoCapture]
param([switch]$Assets, [switch]$NoCapture, [int]$ResX = 1600, [int]$ResY = 900)
$ErrorActionPreference = "Continue"
$p = Split-Path -Parent $PSScriptRoot
$ue = Join-Path (& "$PSScriptRoot\find_ue.ps1") "Engine"
$proj = "$p\PixelArt3D.uproject"

$build = & "$ue\Build\BatchFiles\Build.bat" PixelArt3DEditor Win64 Development -Project="$proj" -WaitMutex 2>&1
$build | Select-String -Pattern "error C|error LNK|Result:" | ForEach-Object { $_.Line }
if (-not ($build | Select-String -Pattern "Result: Succeeded")) { "BUILD FAILED - stopping"; exit 1 }

if ($Assets) {
    # .gitattributes marks *.uasset lockable, so git LFS leaves them read-only after a commit
    Get-ChildItem "$p\Content" -Recurse -File | Where-Object { $_.IsReadOnly } | ForEach-Object { $_.IsReadOnly = $false }
    & "$ue\Binaries\Win64\UnrealEditor-Cmd.exe" "$proj" -run=pythonscript -script="$p\Tools\build_assets.py" -unattended -nop4 -nosplash -stdout 2>&1 |
        Select-String -Pattern "build_assets|LogPython: Error" | ForEach-Object { $_.Line }
}

if (-not $NoCapture) {
    $out = "$p\Saved\Capture"
    New-Item -ItemType Directory -Force $out | Out-Null
    Get-ChildItem $out -Filter *.png | Remove-Item -Force
    $proc = Start-Process -FilePath "$ue\Binaries\Win64\UnrealEditor.exe" -ArgumentList @("`"$proj`"", "-game", "-windowed", "-ResX=$ResX", "-ResY=$ResY", "-PixelCapture=$out", "-RenderOffScreen", "-ForceRes", "-ExecCmds=`"r.SetRes ${ResX}x${ResY}w`"", "-unattended", "-nosound", "-nosplash", "-log=capture.log") -PassThru
    if (-not $proc.WaitForExit(480000)) { $proc.Kill(); "capture timeout" }
    Get-ChildItem $out -Filter *.png | ForEach-Object { $_.Name }
    Select-String -Path "$p\Saved\Logs\capture.log" -Pattern "PixelAnim|Error: .*Material|Shader.*error" | Select-Object -First 10 | ForEach-Object { $_.Line }
}
