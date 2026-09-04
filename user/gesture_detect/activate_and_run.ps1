# Activate conda env and run the UI
$CondaRoot = "D:\anaconda"
$EnvName = "hand_action"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

$env:PATH = "$CondaRoot\envs\$EnvName;$CondaRoot\envs\$EnvName\Library\bin;$CondaRoot\envs\$EnvName\Scripts;" + $env:PATH
Set-Location $ProjectRoot
& "$CondaRoot\envs\$EnvName\python.exe" .\CVideo.py
