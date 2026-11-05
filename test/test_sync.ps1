# Синхронизация между двумя узлами: обнаружение, файлы в обе стороны,
# вложенные папки, манифест/журнал, ручное добавление узла.
# Пара узлов поднимается сама — заранее ничего запускать не нужно.
. (Join-Path $PSScriptRoot 'common.ps1')

Stop-AllTestNodes
$pair = Start-TestPair
$A = $pair.A.Base
$B = $pair.B.Base
$shareA = $pair.ShareA
$shareB = $pair.ShareB
function enc([string]$s) { [System.Uri]::EscapeDataString($s) }

try {
    Write-Host '--- 1. обнаружение узлов (UDP) ---'
    $okPeers = $false
    $aSees = $null
    $bSees = $null
    for ($i = 0; $i -lt 40; $i++) {
        $sa = Invoke-RestMethod "$A/api/status"
        $sb = Invoke-RestMethod "$B/api/status"
        $aSees = ($sa.peers | Where-Object { $_.name -eq 'Node-B' -and $_.online })
        $bSees = ($sb.peers | Where-Object { $_.name -eq 'Node-A' -and $_.online })
        if ($aSees -and $bSees) { $okPeers = $true; break }
        Start-Sleep -Milliseconds 500
    }
    "А видит B: $([bool]$aSees)   B видит A: $([bool]$bSees)"
    if ($aSees) { "адрес B: $($aSees.ip):$($aSees.http) / ftp $($aSees.ftp)" }
    Assert-True $okPeers 'узлы не увидели друг друга'

    Write-Host '--- 2. синхронизация B -> A (файл положен на диск у B) ---'
    $srcB = Join-Path $shareB 'from-b.txt'
    $dstA = Join-Path $shareA 'from-b.txt'
    Remove-Item -LiteralPath $dstA -ErrorAction SilentlyContinue
    $stampB = [DateTime]::Now.AddMinutes(-90)
    Set-Content -Path $srcB -Value 'файл создан на узле B' -Encoding UTF8
    (Get-Item $srcB).LastWriteTime = $stampB
    $expected = (Get-Item $srcB).Length

    $ok = Wait-TestPath -Path $dstA -Exists $true -Seconds 30
    "файл появился на A: $ok"
    Assert-True $ok 'файл с B не приехал на A'
    $lenA = (Get-Item $dstA).Length
    $mtA = (Get-Item $dstA).LastWriteTime
    $diff = [math]::Abs(($mtA - $stampB).TotalSeconds)
    "размер: B=$expected A=$lenA  mtime B=$($stampB.ToString('HH:mm:ss')) A=$($mtA.ToString('HH:mm:ss')) (разница $([math]::Round($diff)) c)"
    Assert-True ($lenA -eq $expected) 'размер файла на A не совпал с B'
    Assert-True ($diff -le 2) 'mtime файла не перенесён с B'
    $sameContent = ((Get-Content $dstA -Raw).Trim() -eq (Get-Content $srcB -Raw).Trim())
    "содержимое совпадает: $sameContent"
    Assert-True $sameContent 'содержимое файла на A отличается'

    Write-Host '--- 3. синхронизация A -> B (загрузка через GUI-API) ---'
    $dstB = Join-Path $shareB 'from-a.txt'
    Remove-Item -LiteralPath $dstB -ErrorAction SilentlyContinue
    $body = [System.Text.Encoding]::UTF8.GetBytes('файл, отправленный через HTTP на A')
    $up = Invoke-RestMethod -Uri "$A/api/upload?path=&name=$(enc 'from-a.txt')" -Method Post -Body $body -ContentType 'application/octet-stream'
    "загрузка на A: ok=$($up.ok)"
    Assert-True ([bool]$up.ok) 'загрузка на A не подтверждена'
    $ok2 = Wait-TestPath -Path $dstB -Exists $true -Seconds 30
    "файл появился на B: $ok2"
    Assert-True $ok2 'файл с A не приехал на B'
    "содержимое на B: $((Get-Content $dstB -Raw).Trim())"

    Write-Host '--- 4. вложенная папка ---'
    $nested = Join-Path $shareB 'sub\deep'
    New-Item -ItemType Directory -Force -Path $nested | Out-Null
    Set-Content -Path (Join-Path $nested 'deep.txt') -Value 'глубоко' -Encoding UTF8
    $dstNested = Join-Path $shareA 'sub\deep\deep.txt'
    $ok3 = Wait-TestPath -Path $dstNested -Exists $true -Seconds 30
    "вложенный файл приехал на A: $ok3"
    Assert-True $ok3 'вложенный файл не приехал на A'

    Write-Host '--- 5. манифест и журнал ---'
    $manA = Invoke-RestMethod "$A/api/manifest"
    "манифест A ($(@($manA).Count)): " + ((@($manA) | ForEach-Object { $_.p }) -join ', ')
    Assert-True (@($manA).Count -ge 3) 'в манифесте A меньше трёх файлов'
    $stA = Invoke-RestMethod "$A/api/status"
    "передач в журнале A: $(@($stA.transfers).Count), получено байт: $($stA.stats.bytesIn)"
    Assert-True ($stA.stats.bytesIn -gt 0) 'счётчик принятых байт не вырос'
    @($stA.transfers) | Select-Object -Last 5 | ForEach-Object { "  $($_.dir) $($_.file) [$($_.status)] $($_.peer)" }

    Write-Host '--- 6. ручное добавление узла ---'
    $add = Invoke-RestMethod -Uri "$A/api/peers" -Method Post -Body '{"addr":"127.0.0.1:8092"}' -ContentType 'application/json'
    "повторное добавление B: ok=$($add.ok)"
    Assert-True ([bool]$add.ok) 'повторное добавление знакомого узла не прошло'
    $bad = Invoke-WebRequest -Uri "$A/api/peers" -Method Post -Body '{"addr":"127.0.0.1:9999"}' -ContentType 'application/json' -SkipHttpErrorCheck
    "недоступный узел -> $($bad.StatusCode)"
    Assert-True ($bad.StatusCode -eq 400) 'недоступный узел принят без ошибки'
}
finally {
    Stop-TestNode @($pair.A, $pair.B)
}

Write-Host 'ALL SYNC TESTS DONE'
