# Кнопка «Синхронизировать»: узел сам выкладывает ВСЕ свои папки соседям.
# Проверяем три правила:
#   1. нажатие отправляет все папки узла, а не одну выбранную;
#   2. после нажатия файлы сразу оказываются на других устройствах;
#   3. нажатие не командует соседом: папку, которая лежит на B, отправляет
#      только B — нажатие на A её не забирает и не трогает.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\obmen.exe'
$t = Join-Path $root 'test'
$A = 'http://127.0.0.1:8141'
$B = 'http://127.0.0.1:8142'
$dirs = @{
    'A-Общая' = Join-Path $t 'shareNowA1'
    'A-Фото'  = Join-Path $t 'shareNowA2'
    'B-Общая' = Join-Path $t 'shareNowB1'
    'B-Фото'  = Join-Path $t 'shareNowB2'
}
$cfgA = Join-Path $t 'now-a.ini'
$cfgB = Join-Path $t 'now-b.ini'

function Stop-Nodes {
    Get-Process obmen -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 700
}

function Post([string]$uri) {
    Invoke-RestMethod -Uri $uri -Method Post -TimeoutSec 10 | Out-Null
}

function Wait-File([string]$path, [bool]$exists, [int]$seconds = 10) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path $path) -eq $exists) { return $true }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

$fail = @()
function Check([string]$what, [bool]$ok) {
    if ($ok) { Write-Host ("  OK   " + $what) } else { Write-Host ("  FAIL " + $what); $script:fail += $what }
}

Stop-Nodes
foreach ($d in $dirs.Values) {
    if (Test-Path $d) { Remove-Item -Recurse -Force $d }
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}
Remove-Item -Force $cfgA, $cfgB -ErrorAction SilentlyContinue

# Интервал намеренно большой: всё, что появится у соседа, может приехать только
# от нажатия кнопки, а не от фонового цикла.
@(
    '# узел A теста кнопки'
    "share=Общая|$($dirs['A-Общая'])"
    "share=Фото|$($dirs['A-Фото'])"
    'name=Node-A'
    'http_port=8141'
    'ftp_port=2171'
    'udp_port=45651'
    'ftp_enabled=0'
    'sync_enabled=1'
    'sync_interval=60'
) | Set-Content -Path $cfgA -Encoding UTF8

$null = Start-Process -FilePath $exe -ArgumentList @('--config', $cfgA, '--no-ui') -PassThru -WindowStyle Hidden
$null = Start-Process -FilePath $exe -ArgumentList @(
    '--share', "Общая=$($dirs['B-Общая'])", '--share', "Фото=$($dirs['B-Фото'])",
    '--name', 'Node-B', '--port', '8142', '--ftp-port', '2172', '--udp-port', '45651',
    '--interval', '60', '--no-ftp', '--no-ui') -PassThru -WindowStyle Hidden

try {
    $seen = $false
    for ($i = 0; $i -lt 40; $i++) {
        try {
            $sa = Invoke-RestMethod "$A/api/status" -TimeoutSec 2
            $sb = Invoke-RestMethod "$B/api/status" -TimeoutSec 2
            $aSees = $sa.peers | Where-Object { $_.name -eq 'Node-B' -and $_.online }
            $bSees = $sb.peers | Where-Object { $_.name -eq 'Node-A' -and $_.online }
            if ($aSees -and $bSees) { $seen = $true; break }
        } catch { }
        Start-Sleep -Milliseconds 500
    }
    Check 'узлы нашли друг друга' $seen

    Write-Host '--- 3. папка, которая лежит на B, нажатием на A не синхронизируется ---'
    $bOnly = Join-Path $dirs['B-Общая'] 'b-only.txt'
    Set-Content -Path $bOnly -Value 'файл создан на узле B' -Encoding UTF8
    $aCopy = Join-Path $dirs['A-Общая'] 'b-only.txt'
    Post "$A/api/sync/now"
    Start-Sleep -Seconds 3
    Check 'нажатие на A не забрало файл с B' (-not (Test-Path $aCopy))

    Write-Host '--- 3b. нажатие на B отправляет его папку всем ---'
    Post "$B/api/sync/now"
    Check 'файл с B появился на A после нажатия на B' (Wait-File $aCopy $true)
    if (Test-Path $aCopy) {
        Check 'содержимое совпадает' ((Get-Content $aCopy -Raw).Trim() -eq 'файл создан на узле B')
    }

    Write-Host '--- 1+2. одно нажатие на A отправляет ВСЕ папки A ---'
    $plain = Join-Path $dirs['A-Общая'] 'from-a.txt'
    $photo = Join-Path $dirs['A-Фото'] 'foto-a.txt'
    Set-Content -Path $plain -Value 'файл из «Общей» узла A' -Encoding UTF8
    Set-Content -Path $photo -Value 'файл из «Фото» узла A' -Encoding UTF8
    Post "$A/api/sync/now"
    Check 'файл «Общей» сразу приехал на B' (Wait-File (Join-Path $dirs['B-Общая'] 'from-a.txt') $true)
    Check 'файл «Фото» сразу приехал на B' (Wait-File (Join-Path $dirs['B-Фото'] 'foto-a.txt') $true)
    Check '«Фото» не попало в «Общую» на B' (-not (Test-Path (Join-Path $dirs['B-Общая'] 'foto-a.txt')))
    Check '«Общая» не попала в «Фото» на B' (-not (Test-Path (Join-Path $dirs['B-Фото'] 'from-a.txt')))

    Write-Host '--- 4. вложенный путь ---'
    $deep = Join-Path $dirs['A-Общая'] 'sub\deep'
    New-Item -ItemType Directory -Force -Path $deep | Out-Null
    Set-Content -Path (Join-Path $deep 'deep.txt') -Value 'глубоко' -Encoding UTF8
    Post "$A/api/sync/now"
    Check 'вложенный файл приехал на B' (Wait-File (Join-Path $dirs['B-Общая'] 'sub\deep\deep.txt') $true)

    Write-Host '--- 5. удаление на A сразу уходит на B ---'
    Post ("$A/api/delete?share=" + [uri]::EscapeDataString('Общая') + "&path=from-a.txt")
    Post "$A/api/sync/now"
    Check 'удалённый на A файл пропал на B' (Wait-File (Join-Path $dirs['B-Общая'] 'from-a.txt') $false)
    Check 'остальные файлы на месте' (Test-Path (Join-Path $dirs['B-Фото'] 'foto-a.txt'))

    Write-Host '--- 6. журнал передач ---'
    $sb2 = Invoke-RestMethod "$B/api/status" -TimeoutSec 5
    $sb2.transfers | Select-Object -Last 6 | ForEach-Object { "  B: $($_.dir) $($_.file) [$($_.status)] $($_.peer)" }
    $sa2 = Invoke-RestMethod "$A/api/status" -TimeoutSec 5
    $sa2.transfers | Select-Object -Last 6 | ForEach-Object { "  A: $($_.dir) $($_.file) [$($_.status)] $($_.peer)" }
}
finally {
    Stop-Nodes
    Remove-Item -Force $cfgA, $cfgB -ErrorAction SilentlyContinue
}

if ($fail.Count -gt 0) {
    Write-Host ("ПРОВАЛЕНО: " + ($fail -join '; '))
    exit 1
}
Write-Host 'ALL SYNC-NOW TESTS DONE'
