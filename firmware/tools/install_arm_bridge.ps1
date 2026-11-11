# Starts arm_bridge.py in the background every time you log in to Windows,
# and restarts it if it stops. Run once from PowerShell:
#   powershell -ExecutionPolicy Bypass -File tools\install_arm_bridge.ps1 [-Port COM5]
# Save the armctl login first: python tools\arm_bridge.py --save-login
# Remove with: Unregister-ScheduledTask -TaskName PuckArmBridge -Confirm:$false
param(
  [string]$Port = "COM5",
  [string]$Url = "http://127.0.0.1:7002"
)

$python = (Get-Command python -ErrorAction Stop).Source
$pythonw = Join-Path (Split-Path $python) "pythonw.exe"
if (-not (Test-Path $pythonw)) { $pythonw = $python }
$bridge = Join-Path $PSScriptRoot "arm_bridge.py"
$log = Join-Path $env:LOCALAPPDATA "smart-puck\arm_bridge.log"
New-Item -ItemType Directory -Force -Path (Split-Path $log) | Out-Null

$action = New-ScheduledTaskAction -Execute $pythonw `
  -Argument "`"$bridge`" $Port --url $Url --log `"$log`"" -WorkingDirectory $PSScriptRoot
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
  -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1)
Register-ScheduledTask -TaskName "PuckArmBridge" -Action $action -Trigger $trigger -Settings $settings `
  -Description "Smart Puck: relays the puck's Robot arm mode to armctl" -Force | Out-Null
Start-ScheduledTask -TaskName "PuckArmBridge"
Write-Host "PuckArmBridge installed and started. Log: $log"
