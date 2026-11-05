# Распространение удалений: файл, папка с вложенным файлом, «воскрешение»
# файла после повторного создания. Пара узлов поднимается сама.
. (Join-Path $PSScriptRoot 'common.ps1')

Stop-AllTestNodes
$pair = Start-TestPair
$A = $pair.A.Base
$B = $pair.B.Base
$shareA = $pair.ShareA
$shareB = $pair.ShareB
function enc([string]$s) { [System.Uri]::EscapeDataString($s) }

try {
    Write-Host '--- 0. обнаружение ---'
    $peers = 0
    for ($i = 0; $i -lt 40; $i++) {
        $sa = Invoke-RestMethod "$A/api/status"
        $peers = @($sa.peers | Where-Object { $_.online }).Count
        if ($peers -gt 0) { break }
        Start-Sleep -Milliseconds 500
    }
    "узлов у A: $peers"
    Assert-True ($peers -gt 0) 'узлы не увидели друг друга'

    Write-Host '--- 1. создаём файл на A (HTTP) и ждём его у B ---'
    $body = [System.Text.Encoding]::UTF8.GetBytes('этот файл будет удалён')
    $null = Invoke-RestMethod -Uri "$A/api/upload?path=&name=$(enc 'to-delete.txt')" -Method Post -Body $body -ContentType 'application/octet-stream'
    $fB = Join-Path $shareB 'to-delete.txt'
    $appeared = Wait-TestPath -Path $fB -Exists $true
    "файл появился на B: $appeared"
    Assert-True $appeared 'файл не приехал на B'

    Write-Host '--- 2. удаляем на A ---'
    $null = Invoke-RestMethod -Uri "$A/api/delete?path=$(enc 'to-delete.txt')" -Method Post
    $gone = Wait-TestPath -Path $fB -Exists $false
    "пропал на B: $gone"
    Assert-True $gone 'удаление не дошло до B'
    $fA = Join-Path $shareA 'to-delete.txt'
    "нет и на A: $(-not (Test-Path -LiteralPath $fA))"
    Assert-True (-not (Test-Path -LiteralPath $fA)) 'файл остался на A'

    Write-Host '--- 3. проверяем, что не возвращается (12 с) ---'
    Start-Sleep -Seconds 12
    "на A: файл есть = $(Test-Path -LiteralPath $fA); на B: файл есть = $(Test-Path -LiteralPath $fB)"
    Assert-True (-not (Test-Path -LiteralPath $fA) -and -not (Test-Path -LiteralPath $fB)) 'файл вернулся синхронизацией'

    Write-Host '--- 4. надгробия ---'
    $tA = Invoke-RestMethod "$A/api/tombstones"
    $tB = Invoke-RestMethod "$B/api/tombstones"
    "A: $(@($tA).Count) надгробий: " + ((@($tA) | ForEach-Object { "$($_.p)@$($_.t)" }) -join ', ')
    "B: $(@($tB).Count) надгробий: " + ((@($tB) | ForEach-Object { "$($_.p)@$($_.t)" }) -join ', ')
    Assert-True (@($tA).Count -ge 1) 'у A нет надгробия удалённого файла'

    Write-Host '--- 5. удаление папки с вложенным файлом ---'
    $dirB = Join-Path $shareB 'to-delete-dir'
    New-Item -ItemType Directory -Force -Path (Join-Path $dirB 'inner') | Out-Null
    Set-Content -Path (Join-Path $dirB 'inner\x.txt') -Value 'вложенный' -Encoding UTF8
    $dirA = Join-Path $shareA 'to-delete-dir'
    $dirArrived = Wait-TestPath -Path (Join-Path $dirA 'inner\x.txt') -Exists $true
    "папка приехала на A: $dirArrived"
    Assert-True $dirArrived 'папка не приехала на A'
    $null = Invoke-RestMethod -Uri "$A/api/delete?path=$(enc 'to-delete-dir')" -Method Post
    $dirGone = Wait-TestPath -Path $dirB -Exists $false
    "папка исчезла на B: $dirGone"
    Assert-True $dirGone 'папка не удалилась на B'
    Start-Sleep -Seconds 8
    "папка не вернулась на A: $(-not (Test-Path -LiteralPath $dirA)); на B: $(-not (Test-Path -LiteralPath $dirB))"
    Assert-True (-not (Test-Path -LiteralPath $dirA) -and -not (Test-Path -LiteralPath $dirB)) 'папка вернулась синхронизацией'

    Write-Host '--- 6. файл создан заново после удаления ---'
    $body2 = [System.Text.Encoding]::UTF8.GetBytes('новая версия после удаления')
    $null = Invoke-RestMethod -Uri "$A/api/upload?path=&name=$(enc 'revive.txt')" -Method Post -Body $body2 -ContentType 'application/octet-stream'
    $rvB = Join-Path $shareB 'revive.txt'
    $rv1 = Wait-TestPath -Path $rvB -Exists $true
    "новая версия доехала до B: $rv1"
    Assert-True $rv1 'файл revive.txt не доехал до B'
    $null = Invoke-RestMethod -Uri "$A/api/delete?path=$(enc 'revive.txt')" -Method Post
    $rv2 = Wait-TestPath -Path $rvB -Exists $false
    "удалена на B: $rv2"
    Assert-True $rv2 'revive.txt не удалился на B'
    Start-Sleep -Seconds 2
    $body3 = [System.Text.Encoding]::UTF8.GetBytes('возрождённый файл')
    $null = Invoke-RestMethod -Uri "$A/api/upload?path=&name=$(enc 'revive.txt')" -Method Post -Body $body3 -ContentType 'application/octet-stream'
    $rv3 = Wait-TestPath -Path $rvB -Exists $true
    "пересозданный файл доехал до B: $rv3"
    Assert-True $rv3 'файл не «воскрес» на B после повторной загрузки'
    if (Test-Path -LiteralPath $rvB) { "содержимое на B: $((Get-Content $rvB -Raw).Trim())" }

    Write-Host '--- 7. состояние хранилища ---'
    Get-ChildItem $shareA -Force | Select-Object Name, Length | Format-Table -AutoSize | Out-String | Write-Host
    $stateA = Join-Path $shareA '.obmen-state.json'
    "файл состояния A существует: $(Test-Path -LiteralPath $stateA)"
    Assert-True (Test-Path -LiteralPath $stateA) 'нет .obmen-state.json с надгробиями'
}
finally {
    Stop-TestNode @($pair.A, $pair.B)
}

Write-Host 'ALL DELETE-PROPAGATION TESTS DONE'
