$ErrorActionPreference = 'Stop'
$A = 'http://127.0.0.1:8091'
$B = 'http://127.0.0.1:8092'
$shareA = 'G:\obmen\test\share1'
$shareB = 'G:\obmen\test\share2'
function enc([string]$s) { [System.Uri]::EscapeDataString($s) }

function Wait-Path {
    param([string]$path, [bool]$exists, [int]$seconds = 30)
    for ($i = 0; $i -lt ($seconds * 2); $i++) {
        if ((Test-Path $path) -eq $exists) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

Write-Host '--- 0. обнаружение ---'
for ($i = 0; $i -lt 20; $i++) {
    $sa = Invoke-RestMethod "$A/api/status"
    if ($sa.peers | Where-Object { $_.name -eq 'Node-B' -and $_.online }) { break }
    Start-Sleep -Milliseconds 500
}
"узлов у A: $(($sa.peers | Where-Object { $_.online }).Count)"

Write-Host '--- 1. создаём файл на A (HTTP) и ждём его на B ---'
$body = [System.Text.Encoding]::UTF8.GetBytes('этот файл будет удалён')
Invoke-RestMethod -Uri "$A/api/upload?path=&name=$(enc 'to-delete.txt')" -Method Post -Body $body -ContentType 'application/octet-stream' | Out-Null
$fB = Join-Path $shareB 'to-delete.txt'
"файл появился на B: $(Wait-Path $fB $true)"

Write-Host '--- 2. удаляем на A ---'
Invoke-RestMethod -Uri "$A/api/delete?path=$(enc 'to-delete.txt')" -Method Post | Out-Null
"пропал на B: $(Wait-Path $fB $false)"
$fA = Join-Path $shareA 'to-delete.txt'
"нет и на A: $(-not (Test-Path $fA))"

Write-Host '--- 3. проверяем, что не возвращается (12 с) ---'
Start-Sleep -Seconds 12
"на A: файл есть = $(Test-Path $fA); на B: файл есть = $(Test-Path $fB)"

Write-Host '--- 4. надгробия ---'
$tA = Invoke-RestMethod "$A/api/tombstones"
$tB = Invoke-RestMethod "$B/api/tombstones"
"A: $($tA.Count) надгробий: " + (($tA | ForEach-Object { "$($_.p)@$($_.t)" }) -join ', ')
"B: $($tB.Count) надгробий: " + (($tB | ForEach-Object { "$($_.p)@$($_.t)" }) -join ', ')

Write-Host '--- 5. удаление папки с вложенным файлом ---'
$dirB = Join-Path $shareB 'to-delete-dir'
New-Item -ItemType Directory -Force -Path (Join-Path $dirB 'inner') | Out-Null
Set-Content -Path (Join-Path $dirB 'inner\x.txt') -Value 'вложенный' -Encoding UTF8
$dirA = Join-Path $shareA 'to-delete-dir'
"папка приехала на A: $(Wait-Path (Join-Path $dirA 'inner\x.txt') $true)"
Invoke-RestMethod -Uri "$A/api/delete?path=$(enc 'to-delete-dir')" -Method Post | Out-Null
"папка исчезла на B: $(Wait-Path $dirB $false)"
Start-Sleep -Seconds 8
"папка не вернулась на A: $(-not (Test-Path $dirA)); на B: $(-not (Test-Path $dirB))"

Write-Host '--- 6. файл создан заново после удаления ---'
$body2 = [System.Text.Encoding]::UTF8.GetBytes('новая версия после удаления')
Invoke-RestMethod -Uri "$A/api/upload?path=&name=$(enc 'revive.txt')" -Method Post -Body $body2 -ContentType 'application/octet-stream' | Out-Null
$rvB = Join-Path $shareB 'revive.txt'
"новая версия доехала до B: $(Wait-Path $rvB $true)"
Invoke-RestMethod -Uri "$A/api/delete?path=$(enc 'revive.txt')" -Method Post | Out-Null
"удалена на B: $(Wait-Path $rvB $false)"
Start-Sleep -Seconds 2
$body3 = [System.Text.Encoding]::UTF8.GetBytes('возрождённый файл')
Invoke-RestMethod -Uri "$A/api/upload?path=&name=$(enc 'revive.txt')" -Method Post -Body $body3 -ContentType 'application/octet-stream' | Out-Null
"пересозданный файл доехал до B: $(Wait-Path $rvB $true)"
if (Test-Path $rvB) { "содержимое на B: $((Get-Content $rvB -Raw).Trim())" }

Write-Host '--- 7. состояние хранилища ---'
Get-ChildItem $shareA -Force | Select-Object Name, Length | Format-Table -AutoSize | Out-String | Write-Host
$stateA = Join-Path $shareA '.obmen-state.json'
"файл состояния A существует: $(Test-Path $stateA)"
if (Test-Path $stateA) { "содержимое: $((Get-Content $stateA -Raw))" }

Write-Host 'ALL DELETE-PROPAGATION TESTS DONE'
