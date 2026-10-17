# Stop the Strata server (serve.server) and its engine, leaving nothing behind.
# Used by start-2gpu.cmd and stop-2gpu.cmd; also handy on its own:
#   powershell -ExecutionPolicy Bypass -File stop-strata.ps1
$ErrorActionPreference = 'SilentlyContinue'

foreach ($p in (Get-CimInstance Win32_Process -Filter "Name='python.exe'" |
                Where-Object { $_.CommandLine -like '*serve.server*' })) {
    "stop serve.server pid $($p.ProcessId)"
    Stop-Process -Id $p.ProcessId -Force
}
foreach ($n in 'strata', 'strata-vision') {
    Get-Process $n | Stop-Process -Force
}

Start-Sleep -Milliseconds 800
$left = @(Get-Process strata -ErrorAction SilentlyContinue).Count
$port = @(Get-NetTCPConnection -LocalPort 8184 -State Listen -ErrorAction SilentlyContinue).Count
"left: strata=$left  listeners_8184=$port"
