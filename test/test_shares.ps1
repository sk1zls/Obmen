# Проверка нескольких сетевых папок: API, синхронизация по именам, FTP-корень.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\obmen.exe'
$t = Join-Path $root 'test'

$shareA = Join-Path $t 'shareA'
$shareB = Join-Path $t 'shareB'
$shareC = Join-Path $t 'shareC'
$shareD = Join-Path $t 'shareD'
$cfgA = Join-Path $t 'a.ini'
$cfgB = Join-Path $t 'b.ini'

function Stop-Nodes {
    Get-Process obmen -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 700
}

Stop-Nodes
foreach ($d in @($shareA, $shareB, $shareC, $shareD)) {
    if (Test-Path $d) { Remove-Item -Recurse -Force $d }
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}
Remove-Item -Force $cfgA, $cfgB -ErrorAction SilentlyContinue

Set-Content -Path (Join-Path $shareA 'a-only.txt') -Value 'файл только в общей папке A' -Encoding UTF8
Set-Content -Path (Join-Path $shareA 'notes.md') -Value '# заметки A' -Encoding UTF8
Set-Content -Path (Join-Path $shareB 'foto-1.txt') -Value 'файл только в папке Фото (A)' -Encoding UTF8

Write-Host '--- запуск двух узлов, по две папки у каждого ---'
$pa = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -ArgumentList @(
    '--share', "Общая=$shareA", '--share', "Фото=$shareB",
    '--port', '8091', '--ftp-port', '2122', '--udp-port', '45554',
    '--name', 'Node-A', '--config', $cfgA, '--no-ui', '--interval', '3')
$pb = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -ArgumentList @(
    '--share', "Общая=$shareC", '--share', "Фото=$shareD",
    '--port', '8092', '--ftp-port', '2123', '--udp-port', '45554',
    '--name', 'Node-B', '--config', $cfgB, '--no-ui', '--interval', '3')

try {
    $ok = $false
    for ($i = 0; $i -lt 40; $i++) {
        try { Invoke-RestMethod 'http://127.0.0.1:8091/api/status' -TimeoutSec 2 | Out-Null; $ok = $true; break } catch { Start-Sleep -Milliseconds 400 }
    }
    if (-not $ok) { throw 'узел A не отвечает' }

    $sa = Invoke-RestMethod 'http://127.0.0.1:8091/api/status'
    $sb = Invoke-RestMethod 'http://127.0.0.1:8092/api/status'
    Write-Host ("A: папки = " + (($sa.shares | ForEach-Object { $_.name + ' → ' + $_.path }) -join '; '))
    Write-Host ("B: папки = " + (($sb.shares | ForEach-Object { $_.name + ' → ' + $_.path }) -join '; '))
    if ($sa.shares.Count -ne 2 -or $sb.shares.Count -ne 2) { throw 'ожидалось по две папки на узле' }

    Write-Host ''
    Write-Host '--- список файлов по папкам (A) ---'
    foreach ($name in @('Общая', 'Фото')) {
        $r = Invoke-RestMethod ('http://127.0.0.1:8091/api/files?share=' + [uri]::EscapeDataString($name) + '&path=')
        Write-Host ("  [" + $name + "] " + (($r.entries | ForEach-Object { $_.name }) -join ', '))
    }

    Write-Host ''
    Write-Host '--- ожидание синхронизации по именам папок ---'
    $deadline = (Get-Date).AddSeconds(40)
    $synced = $false
    while ((Get-Date) -lt $deadline) {
        $cGeneral = @(Get-ChildItem -Path $shareC -File -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
        $dPhoto = @(Get-ChildItem -Path $shareD -File -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
        if (($cGeneral -contains 'a-only.txt') -and ($dPhoto -contains 'foto-1.txt')) { $synced = $true; break }
        Start-Sleep -Milliseconds 700
    }
    $cAfter = @(Get-ChildItem -Path $shareC -File -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
    $dAfter = @(Get-ChildItem -Path $shareD -File -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
    Write-Host ("  B/Общая: " + ($cAfter -join ', '))
    Write-Host ("  B/Фото : " + ($dAfter -join ', '))
    if (-not $synced) { throw 'файлы не разошлись по одноимённым папкам' }
    if ($cAfter -contains 'foto-1.txt') { throw 'файл из «Фото» попал в «Общую»' }
    if ($dAfter -contains 'a-only.txt') { throw 'файл из «Общей» попал в «Фото»' }
    Write-Host '  изоляция папок соблюдена: OK'

    Write-Host ''
    Write-Host '--- FTP: корень и переходы ---'
    $client = New-Object System.Net.Sockets.TcpClient('127.0.0.1', 2122)
    $stream = $client.GetStream()
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $reader = New-Object System.IO.StreamReader($stream, $utf8)
    $writer = New-Object System.IO.StreamWriter($stream, $utf8)
    $writer.NewLine = "`r`n"
    $writer.AutoFlush = $true
    function Read-Reply {
        $line = $reader.ReadLine()
        while ($line -match '^\d{3}-') { $line = $reader.ReadLine() }
        return $line
    }
    function Send-Cmd([string]$c) { $writer.WriteLine($c); return (Read-Reply) }
    function Get-Listing {
        $r = Send-Cmd 'PASV'
        if ($r -notmatch '\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)') { return @() }
        $port = [int]$Matches[5] * 256 + [int]$Matches[6]
        $data = New-Object System.Net.Sockets.TcpClient('127.0.0.1', $port)
        $ds = $data.GetStream()
        $dr = New-Object System.IO.StreamReader($ds, [System.Text.Encoding]::UTF8)
        $null = Send-Cmd 'LIST'
        $text = $dr.ReadToEnd()
        $null = Read-Reply
        $dr.Close(); $data.Close()
        return @($text -split "`r?`n" | Where-Object { $_ -ne '' } | ForEach-Object { ($_ -split '\s+')[-1] })
    }

    $null = Read-Reply
    Write-Host ("  USER: " + (Send-Cmd 'USER anonymous'))
    Write-Host ("  PASS: " + (Send-Cmd 'PASS x'))
    Write-Host ("  PWD : " + (Send-Cmd 'PWD'))
    $top = Get-Listing
    Write-Host ("  корень FTP: " + ($top -join ', '))
    if ($top -notcontains 'Общая' -or $top -notcontains 'Фото') { throw 'в корне FTP нет обеих папок' }

    Write-Host ("  CWD Общая: " + (Send-Cmd 'CWD Общая'))
    Write-Host ("  PWD      : " + (Send-Cmd 'PWD'))
    $inGeneral = Get-Listing
    Write-Host ("  содержимое: " + ($inGeneral -join ', '))
    if ($inGeneral -notcontains 'a-only.txt') { throw 'FTP не видит файл внутри папки «Общая»' }

    Write-Host ("  CWD /Фото: " + (Send-Cmd 'CWD /Фото'))
    $inPhoto = Get-Listing
    Write-Host ("  содержимое: " + ($inPhoto -join ', '))
    if ($inPhoto -notcontains 'foto-1.txt') { throw 'FTP не видит файл внутри папки «Фото»' }

    Write-Host ("  CDUP     : " + (Send-Cmd 'CDUP'))
    Write-Host ("  CDUP     : " + (Send-Cmd 'CDUP'))
    Write-Host ("  PWD      : " + (Send-Cmd 'PWD'))
    $null = Send-Cmd 'QUIT'
    $client.Close()
    Write-Host '  FTP: OK'

    Write-Host ''
    Write-Host '--- веб-интерфейс: список папок ---'
    $edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
    if (-not (Test-Path $edge)) { $edge = 'C:\Program Files\Microsoft\Edge\Application\msedge.exe' }
    if (Test-Path $edge) {
        $dom = Join-Path $t 'dom-shares.html'
        & $edge --headless=new --disable-gpu --no-sandbox --user-data-dir="$t\edge-tmp" `
            --virtual-time-budget=6000 --dump-dom 'http://127.0.0.1:8091/' 2>$null |
            Out-File -FilePath $dom -Encoding utf8
        $html = Get-Content $dom -Raw
        Write-Host ("  DOM: " + $html.Length + " символов")
        foreach ($m in @('Сетевые папки', 'Общая', 'Фото', 'Добавить папку')) {
            Write-Host ("  содержит '" + $m + "': " + $html.Contains($m))
        }
        $rows = [regex]::Matches($html, 'class="share active"').Count
        Write-Host ("  активных папок в списке: " + $rows)

        Write-Host '--- SVG-иконки вместо эмодзи ---'
        $need = @('id="i-folder"', 'id="i-swap"', 'href="#i-swap"', 'class="ic folder"', 'class="ic text"')
        foreach ($m in $need) {
            $okIcon = $html.Contains($m)
            Write-Host ("  '" + $m + "': " + $okIcon)
            if (-not $okIcon) { throw ("нет в DOM: " + $m) }
        }
        foreach ($e in @('📁', '📄', '🖼', '🎬', '🗜')) {
            if ($html.Contains($e)) { throw ("в DOM остался эмодзи: " + $e) }
        }
        Write-Host '  иконки рисуются как SVG, эмодзи нет'

        Remove-Item -Recurse -Force "$t\edge-tmp", $dom -ErrorAction SilentlyContinue
    } else {
        Write-Host '  Edge не найден — пропуск проверки GUI'
    }

    Write-Host ''
    Write-Host 'ИТОГ: все проверки пройдены'
}
finally {
    Stop-Nodes
}
