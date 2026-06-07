# GameMakerC++ API Test Script
# Tests smooth movement via PostMessage-based key simulation

$base = "http://localhost:18080/api"

function Invoke-Api($cmd) {
    try {
        $r = Invoke-RestMethod -Uri "$base?cmd=$cmd" -TimeoutSec 3
        return $r
    } catch {
        Write-Host "Error: $_" -ForegroundColor Red
    }
}

Write-Host "=== GameMakerC++ API Test ===" -ForegroundColor Cyan
Write-Host ""

# Check game status
Write-Host "1. Getting player position..." -ForegroundColor Yellow
Invoke-Api "pos"
Start-Sleep -Milliseconds 500

# Test smooth movement: press key, wait, release
Write-Host "`n2. Smooth move RIGHT for 1 second..." -ForegroundColor Yellow
Invoke-Api "right"
Start-Sleep -Milliseconds 1000
Invoke-Api "stop"
Write-Host "  Position after move:"
Invoke-Api "pos"
Start-Sleep -Milliseconds 500

# Test diagonal movement
Write-Host "`n3. Smooth move UP+RIGHT for 1 second..." -ForegroundColor Yellow
Invoke-Api "up"
Invoke-Api "right"
Start-Sleep -Milliseconds 1000
Invoke-Api "stop"
Write-Host "  Position after diagonal:"
Invoke-Api "pos"
Start-Sleep -Milliseconds 500

# Test WASD keys
Write-Host "`n4. WASD keys for 1 second..." -ForegroundColor Yellow
Invoke-Api "s_down"
Start-Sleep -Milliseconds 500
Invoke-Api "s_up"
Invoke-Api "d_down"
Start-Sleep -Milliseconds 500
Invoke-Api "d_up"
Write-Host "  Position after WASD:"
Invoke-Api "pos"

Write-Host "`n=== Test Complete ===" -ForegroundColor Green
