# Конфликты синхронизации: побеждает более новая версия файла.
# Старая копия не должна затирать свежую правку, и файл не должен «мигать» между узлами.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\obmen.exe'
$t = Join-Path $root 'test'
$shareA = Join-Path $t 'shareA'
$shareB = Join-Path $t 'shareB'
$cfgA = Join-Path $t 'a.ini'
$cfgB = Join-Path $t 'b.ini'
$A = 'http://127.0.0.1:8101'
$B = 'http://127.0.0.1:8102'

function Stop-Nodes {
    Get-Process obmen -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 700
}

function Wait-Text([string]$path, [string]$want, [int]$seconds = 40) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $path) {
            $now = Get-Content $path -Raw -ErrorAction SilentlyContinue
            if ($null -ne $now -and $now.Trim() -eq $want) { return $true }
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Wait-Path([string]$path, [bool]$exists, [int]$seconds = 40) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path $path) -eq $exists) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

Stop-Nodes
foreach ($d in @($shareA, $shareB)) {
    if (Test-Path $d) { Remove-Item -Recurse -Force $d }
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}
Remove-Item -Force $cfgA, $cfgB -ErrorAction SilentlyContinue

# Один и тот же файл на обоих узлах, содержимое разное; на A версия новее.
$fresh = 'версия A: правка свежая, её нельзя затирать'
$stale = 'старая версия B'
$fA = Join-Path $shareA 'conflict.txt'
$fB = Join-Path $shareB 'conflict.txt'
Set-Content -Path $fA -Value $fresh -Encoding UTF8
Set-Content -Path $fB -Value $stale -Encoding UTF8
(Get-Item $fA).LastWriteTime = (Get-Date).AddMinutes(-2)
(Get-Item $fB).LastWriteTime = (Get-Date).AddMinutes(-20)
Write-Host ("--- старт: A новее (" + (Get-Item $fA).LastWriteTime.ToString('HH:mm:ss') +
            "), B старее (" + (Get-Item $fB).LastWriteTime.ToString('HH:mm:ss') + ") ---")

Write-Host '--- запуск двух узлов с одноимённой папкой «Общая» ---'
$pa = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -ArgumentList @(
    '--share', "Общая=$shareA", '--port', '8101', '--ftp-port', '2131', '--udp-port', '45561',
    '--name', 'Node-A', '--config', $cfgA, '--no-ui', '--interval', '3')
$pb = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -ArgumentList @(
    '--share', "Общая=$shareB", '--port', '8102', '--ftp-port', '2132', '--udp-port', '45561',
    '--name', 'Node-B', '--config', $cfgB, '--no-ui', '--interval', '3')

try {
    $deadline = (Get-Date).AddSeconds(30)
    $ready = $false
    while ((Get-Date) -lt $deadline) {
        try {
            $sa = Invoke-RestMethod "$A/api/status" -TimeoutSec 2
            $sb = Invoke-RestMethod "$B/api/status" -TimeoutSec 2
            $seen = ($sa.peers | Where-Object { $_.name -eq 'Node-B' -and $_.online }) -and
                    ($sb.peers | Where-Object { $_.name -eq 'Node-A' -and $_.online })
            if ($seen) { $ready = $true; break }
        } catch { }
        Start-Sleep -Milliseconds 500
    }
    if (-not $ready) { throw 'узлы не увидели друг друга' }
    Write-Host '  узлы нашли друг друга: OK'

    Write-Host '--- 1. старая копия не затирает свежую (A новее) ---'
    $ok1 = Wait-Text $fB $fresh 40
    "  на B теперь свежая версия A: $ok1"
    "  на A версия не изменилась: $((Get-Content $fA -Raw).Trim() -eq $fresh)"
    if (-not $ok1) { throw 'старая копия на B не заменилась свежей версией с A' }

    Write-Host '--- 2. правка на B уезжает на A ---'
    $fromB = 'версия B: пользователь правил файл на втором ПК'
    Set-Content -Path $fB -Value $fromB -Encoding UTF8
    $ok2 = Wait-Text $fA $fromB 40
    "  на A теперь версия B: $ok2"
    if (-not $ok2) { throw 'правка на B не доехала до A' }

    Write-Host '--- 3. новый файл с A приезжает на B сам ---'
    $newFile = Join-Path $shareA 'fresh.txt'
    Set-Content -Path $newFile -Value 'новый файл, созданный на A' -Encoding UTF8
    $ok3 = Wait-Text (Join-Path $shareB 'fresh.txt') 'новый файл, созданный на A' 40
    "  файл появился на B без действий пользователя: $ok3"
    if (-not $ok3) { throw 'новый файл не синхронизировался' }

    Write-Host '--- 4. удаление на B распространяется на A ---'
    Invoke-RestMethod -Uri "$B/api/delete?path=fresh.txt" -Method Post | Out-Null
    $ok4 = Wait-Path $newFile $false 40
    "  файл исчез на A: $ok4"
    if (-not $ok4) { throw 'удаление не распространилось' }

    Write-Host '--- 5. стабильность: файл не перекачивается туда-сюда (12 с) ---'
    $before = (Get-Content $fA -Raw).Trim()
    Start-Sleep -Seconds 12
    $after = (Get-Content $fA -Raw).Trim()
    $parts = @(Get-ChildItem -Path $shareA, $shareB -Filter *.part -ErrorAction SilentlyContinue)
    "  содержимое не изменилось: $($before -eq $after)"
    "  недокачанных .part: $($parts.Count)"
    if ($before -ne $after) { throw 'файл «мигает» между узлами' }
    if ($parts.Count -ne 0) { throw 'остались временные файлы .part' }

    Write-Host '--- 6. журнал A ---'
    $st = Invoke-RestMethod "$A/api/status"
    $st.transfers | Select-Object -Last 4 | ForEach-Object { "  $($_.dir) $($_.file) [$($_.status)] $($_.peer)" }

    Write-Host 'ИТОГ: конфликты решаются по времени изменения, синхронизация идёт сама'
}
finally {
    Stop-Nodes
}
