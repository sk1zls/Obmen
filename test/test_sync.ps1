$ErrorActionPreference = 'Stop'
$A = 'http://127.0.0.1:8091'
$B = 'http://127.0.0.1:8092'
$shareA = 'G:\obmen\test\share1'
$shareB = 'G:\obmen\test\share2'
function enc([string]$s) { [System.Uri]::EscapeDataString($s) }

Write-Host '--- 1. обнаружение узлов (UDP) ---'
$okPeers = $false
for ($i = 0; $i -lt 20; $i++) {
    $sa = Invoke-RestMethod "$A/api/status"
    $sb = Invoke-RestMethod "$B/api/status"
    $aSees = ($sa.peers | Where-Object { $_.name -eq 'Node-B' -and $_.online })
    $bSees = ($sb.peers | Where-Object { $_.name -eq 'Node-A' -and $_.online })
    if ($aSees -and $bSees) { $okPeers = $true; break }
    Start-Sleep -Milliseconds 500
}
"A видит B: $([bool]$aSees)   B видит A: $([bool]$bSees)"
if ($aSees) { "адрес B: $($aSees.ip):$($aSees.http) / ftp $($aSees.ftp)" }

Write-Host '--- 2. синхронизация B -> A (файл положен на диск в share2) ---'
$srcB = Join-Path $shareB 'from-b.txt'
$dstA = Join-Path $shareA 'from-b.txt'
Remove-Item $dstA -ErrorAction SilentlyContinue  # чтобы прогон был детерминированным
$stampB = [DateTime]::Now.AddMinutes(-90)
Set-Content -Path $srcB -Value 'файл создан на узле B' -Encoding UTF8
(Get-Item $srcB).LastWriteTime = $stampB
$expected = (Get-Item $srcB).Length

$dstA = Join-Path $shareA 'from-b.txt'
$ok = $false
for ($i = 0; $i -lt 30; $i++) {
    if (Test-Path $dstA) { $ok = $true; break }
    Start-Sleep -Milliseconds 500
}
"файл появился на A: $ok"
if ($ok) {
    $lenA = (Get-Item $dstA).Length
    $mtA = (Get-Item $dstA).LastWriteTime
    $diff = [math]::Abs(($mtA - $stampB).TotalSeconds)
    "размер: B=$expected A=$lenA  mtime B=$($stampB.ToString('HH:mm:ss')) A=$($mtA.ToString('HH:mm:ss')) (разница $([math]::Round($diff)) c)"
    "содержимое совпадает: $((Get-Content $dstA -Raw).Trim() -eq (Get-Content $srcB -Raw).Trim())"
}

Write-Host '--- 3. синхронизация A -> B (загрузка через GUI-API) ---'
$dstB = Join-Path $shareB 'from-a.txt'
Remove-Item $dstB -ErrorAction SilentlyContinue  # чтобы прогон был детерминированным
$body = [System.Text.Encoding]::UTF8.GetBytes('файл, отправленный через HTTP на A')
$up = Invoke-RestMethod -Uri "$A/api/upload?path=&name=$(enc 'from-a.txt')" -Method Post -Body $body -ContentType 'application/octet-stream'
"загрузка на A: ok=$($up.ok)"
$ok2 = $false
for ($i = 0; $i -lt 30; $i++) {
    if (Test-Path $dstB) { $ok2 = $true; break }
    Start-Sleep -Milliseconds 500
}
"файл появился на B: $ok2"
if ($ok2) { "содержимое на B: $((Get-Content $dstB -Raw).Trim())" }

Write-Host '--- 4. вложенная папка ---'
$nested = Join-Path $shareB 'sub\deep'
New-Item -ItemType Directory -Force -Path $nested | Out-Null
Set-Content -Path (Join-Path $nested 'deep.txt') -Value 'глубоко' -Encoding UTF8
$dstNested = Join-Path $shareA 'sub\deep\deep.txt'
$ok3 = $false
for ($i = 0; $i -lt 30; $i++) {
    if (Test-Path $dstNested) { $ok3 = $true; break }
    Start-Sleep -Milliseconds 500
}
"вложенный файл приехал на A: $ok3"

Write-Host '--- 5. манифест и журнал ---'
$manA = Invoke-RestMethod "$A/api/manifest"
"манифест A ($($manA.Count)): " + (($manA | ForEach-Object { $_.p }) -join ', ')
$stA = Invoke-RestMethod "$A/api/status"
"передач в журнале A: $($stA.transfers.Count), получено байт: $($stA.stats.bytesIn)"
$stA.transfers | Select-Object -Last 5 | ForEach-Object { "  $($_.dir) $($_.file) [$($_.status)] $($_.peer)" }

Write-Host '--- 6. ручное добавление узла ---'
$add = Invoke-RestMethod -Uri "$A/api/peers" -Method Post -Body '{"addr":"127.0.0.1:8092"}' -ContentType 'application/json'
"повторное добавление B: ok=$($add.ok)"
$bad = Invoke-WebRequest -Uri "$A/api/peers" -Method Post -Body '{"addr":"127.0.0.1:9999"}' -ContentType 'application/json' -SkipHttpErrorCheck
"недоступный узел -> $($bad.StatusCode) $($bad.Content)"

Write-Host 'ALL SYNC TESTS DONE'
