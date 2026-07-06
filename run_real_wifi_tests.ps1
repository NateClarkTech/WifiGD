$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

$envFile = ".env"
if (-not (Test-Path $envFile)) {
    Write-Error "Missing $envFile. Copy .env.example to .env and set WIFI_SSID / WIFI_PASSWORD."
}

Get-Content $envFile | ForEach-Object {
    $line = $_.Trim()
    if ($line -eq "" -or $line.StartsWith("#")) { return }
    if (-not $line.Contains("=")) { return }
    $parts = $line.Split("=", 2)
    $key = $parts[0].Trim()
    $value = $parts[1].Trim()
    if ($value.StartsWith('"') -and $value.EndsWith('"')) {
        $value = $value.Substring(1, $value.Length - 2)
    } elseif ($value.StartsWith("'") -and $value.EndsWith("'")) {
        $value = $value.Substring(1, $value.Length - 2)
    }
    [System.Environment]::SetEnvironmentVariable($key, $value, "Process")
}

if ([string]::IsNullOrWhiteSpace($env:WIFI_SSID)) {
    Write-Error "WIFI_SSID is empty in $envFile"
}

$dll = "demo\addons\WifiGD\bin\wifigd.windows.template_debug.x86_64.dll"
if (-not (Test-Path $dll)) {
    Write-Host "Building WifiGD debug library..."
    scons platform=windows
}

Write-Host "Importing project assets..."
godot --headless --path demo --import 2>$null | Out-Null

Write-Host "Running real Wi-Fi connect tests for SSID: $($env:WIFI_SSID)"
Write-Host "Note: on Windows, enable Location in Settings if scan is denied."
Write-Host "Tests avoid disconnecting between cases; connection is restored after the disconnect test."

& godot --headless --path demo `
    -s addons/gut/gut_cmdln.gd `
    -gconfig=res://.gutconfig.json `
    -gexit `
    -gdir=res://tests/live/

$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host "Real Wi-Fi tests passed."
} else {
    Write-Host "Real Wi-Fi tests failed (exit code $exitCode)." -ForegroundColor Red
}

exit $exitCode