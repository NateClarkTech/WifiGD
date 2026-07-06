$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

$dll = "demo\addons\WifiGD\bin\wifigd.windows.template_debug.x86_64.dll"
if (-not (Test-Path $dll)) {
    Write-Host "Building WifiGD debug library..."
    scons platform=windows
}

Write-Host "Importing project assets (required for GUT)..."
godot --headless --path demo --import 2>$null | Out-Null

Write-Host "Running GUT unit + integration tests..."
$gutArgs = @(
    "--headless",
    "--path", "demo",
    "-s", "addons/gut/gut_cmdln.gd",
    "-gconfig=res://.gutconfig.json",
    "-gexit"
) + $args

& godot @gutArgs
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host "Tests passed."
} else {
    Write-Host "Tests failed (exit code $exitCode)." -ForegroundColor Red
}

exit $exitCode