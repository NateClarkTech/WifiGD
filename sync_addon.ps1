# Copies the built WifiGD binaries from addons/WifiGD into demo/addons/WifiGD.
# Close Godot before running this script.

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$source = Join-Path $root "addons\WifiGD"
$target = Join-Path $root "demo\addons\WifiGD"

if (-not (Test-Path $source)) {
    Write-Error "Source addon folder not found: $source"
}

foreach ($subpath in @("", "bin")) {
    $from = Join-Path $source $subpath
    $to = Join-Path $target $subpath
    if (-not (Test-Path $to)) {
        New-Item -ItemType Directory -Path $to -Force | Out-Null
    }
}

Copy-Item (Join-Path $source "WifiGD.gdextension") (Join-Path $target "WifiGD.gdextension") -Force
Copy-Item (Join-Path $source "wifi_manager_autoload.tscn") (Join-Path $target "wifi_manager_autoload.tscn") -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $source "bin\*") (Join-Path $target "bin\") -Force

Write-Host "Synced WifiGD addon to demo/addons/WifiGD"
Get-ChildItem (Join-Path $target "bin\*.dll") | Select-Object Name, Length, LastWriteTime