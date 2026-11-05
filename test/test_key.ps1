# Синхронизация по ключу: папки с разными именами связываются вручную.
# «Games» на A и «игры» на B должны синхронизироваться, если у A задан ключ «игры».
. (Join-Path $PSScriptRoot 'common.ps1')
$root = Split-Path -Parent $PSScriptRoot
$exe = Get-ObmenExe
$t = Join-Path $root 'test'
$shareA = Join-Path $t 'shareKeyA'
$shareB = Join-Path $t 'shareKeyB'
$cfgA = Join-Path $t 'key-a.ini'
$cfgB = Join-Path $t 'key-b.ini'
$A = 'http://127.0.0.1:8131'
$B = 'http://127.0.0.1:8132'

function Stop-Nodes {
    Stop-AllTestNodes
}

function Wait-Path([string]$path, [bool]$exists, [int]$seconds = 40) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path $path) -eq $exists) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Wait-Contents([string]$path, [string]$want, [int]$seconds = 40) {
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

function Post-Json([string]$uri, [string]$json) {
    Invoke-RestMethod -Uri $uri -Method Post -Body ([System.Text.Encoding]::UTF8.GetBytes($json)) `
        -ContentType 'application/json; charset=utf-8' -TimeoutSec 10
}

Stop-Nodes
foreach ($d in @($shareA, $shareB)) {
    if (Test-Path $d) { Remove-Item -Recurse -Force $d }
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}
Remove-Item -Force $cfgA, $cfgB -ErrorAction SilentlyContinue

Write-Host '--- запуск узлов: A «Games», B «игры» ---'
@(
    '# тестовый узел A'
    "share=Games|$shareA"
    'name=Node-A'
    'http_port=8131'
    'ftp_port=2161'
    'udp_port=45641'
    'ftp_enabled=0'
    'sync_enabled=1'
    'sync_interval=3'
) | Set-Content -Path $cfgA -Encoding UTF8

$pa = Start-Process -FilePath $exe -ArgumentList @('--config', $cfgA, '--no-ui') -PassThru
$pb = Start-Process -FilePath $exe -ArgumentList @('--config', $cfgB, '--share', "игры=$shareB", '--name', 'Node-B',
    '--port', '8132', '--ftp-port', '2162', '--udp-port', '45641', '--interval', '3', '--no-ui', '--no-ftp') -PassThru

$okPeers = $false
for ($i = 0; $i -lt 30; $i++) {
    try {
        $sa = Invoke-RestMethod "$A/api/status" -TimeoutSec 2
        if ($sa.peers | Where-Object { $_.name -eq 'Node-B' -and $_.online }) { $okPeers = $true; break }
    } catch { }
    Start-Sleep -Milliseconds 500
}
"  узлы нашли друг друга: $okPeers"

Write-Host '--- 1. привязка Games -> игры (ключ задаётся на одной стороне) ---'
$r = Post-Json "$A/api/shares" '{"action":"setkey","name":"Games","key":"игры"}'
$keyA = ($r.shares | Where-Object { $_.name -eq 'Games' }).key
"  ключ папки Games у A: «$keyA»"

Write-Host '--- 2. файл с A приезжает на B ---'
Set-Content -Path (Join-Path $shareA 'from-a.txt') -Value 'файл с узла A' -Encoding UTF8
"  появился на B: $(Wait-Path (Join-Path $shareB 'from-a.txt') $true)"
"  содержимое на B: $(if (Test-Path (Join-Path $shareB 'from-a.txt')) { (Get-Content (Join-Path $shareB 'from-a.txt') -Raw).Trim() } else { '—' })"

Write-Host '--- 3. файл с B приезжает на A ---'
Set-Content -Path (Join-Path $shareB 'from-b.txt') -Value 'файл с узла B' -Encoding UTF8
"  появился на A: $(Wait-Path (Join-Path $shareA 'from-b.txt') $true)"
"  содержимое на A: $(if (Test-Path (Join-Path $shareA 'from-b.txt')) { (Get-Content (Join-Path $shareA 'from-b.txt') -Raw).Trim() } else { '—' })"

Write-Host '--- 4. удаление на B распространяется на A ---'
Invoke-RestMethod -Uri "$B/api/delete?path=from-a.txt" -Method Post -TimeoutSec 10 | Out-Null
"  пропал на A: $(Wait-Path (Join-Path $shareA 'from-a.txt') $false)"

Write-Host '--- 5. без ключа папки снова независимы ---'
Post-Json "$A/api/shares" '{"action":"setkey","name":"Games","key":""}' | Out-Null
Set-Content -Path (Join-Path $shareB 'only-b.txt') -Value 'только B' -Encoding UTF8
Start-Sleep -Seconds 8
"  файл с B не приехал на A: $(-not (Test-Path (Join-Path $shareA 'only-b.txt')))"

Write-Host '--- 6. состояние ---'
$sa = Invoke-RestMethod "$A/api/status" -TimeoutSec 5
"  A: " + (($sa.shares | ForEach-Object { $_.name + '[' + $_.key + ']' }) -join ', ')
"  A видит ключи B: " + (($sa.peers | ForEach-Object { '[' + ($_.shares -join ',') + ']' }) -join ' ')

Stop-Nodes
Remove-Item -Force $cfgA, $cfgB -ErrorAction SilentlyContinue
Write-Host 'ALL KEY-PAIR TESTS DONE'
