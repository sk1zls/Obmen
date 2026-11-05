# Быстрый переход в окне «Добавить папку»: Документы, Рабочий стол, Изображения,
# Загрузки, Музыка, Видео, домашняя папка, Все диски. Плюс имя папки в сети.
. (Join-Path $PSScriptRoot 'common.ps1')
$root = Split-Path -Parent $PSScriptRoot
$exe = Get-ObmenExe
$t = Join-Path $root 'test'
$share = Join-Path $t 'shareA'
$cfg = Join-Path $t 'places.ini'
$base = 'http://127.0.0.1:8103'
$edgeTmp = Join-Path $t 'edge-places'

function Stop-Nodes {
    Stop-AllTestNodes
}

function Send-Cdp($ws, [int]$id, [string]$method, $params) {
    $msg = @{ id = $id; method = $method; params = $params } | ConvertTo-Json -Depth 10 -Compress
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($msg)
    $ws.SendAsync([System.ArraySegment[byte]]::new($bytes),
        [System.Net.WebSockets.WebSocketMessageType]::Text, $true,
        [System.Threading.CancellationToken]::None).GetAwaiter().GetResult() | Out-Null
    $buf = New-Object byte[] 262144
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $deadline) {
        $sb = New-Object System.Text.StringBuilder
        do {
            $cts = New-Object System.Threading.CancellationTokenSource
            $cts.CancelAfter(10000)
            $seg = [System.ArraySegment[byte]]::new($buf)
            try { $r = $ws.ReceiveAsync($seg, $cts.Token).GetAwaiter().GetResult() }
            catch { return $null }
            [void]$sb.Append([System.Text.Encoding]::UTF8.GetString($buf, 0, $r.Count))
        } while (-not $r.EndOfMessage)
        try { $o = $sb.ToString() | ConvertFrom-Json } catch { continue }
        if ($null -ne $o.id -and $o.id -eq $id) { return $o }
    }
    return $null
}

function Eval-Js($ws, [int]$id, [string]$expr) {
    $r = Send-Cdp $ws $id 'Runtime.evaluate' @{ expression = $expr; returnByValue = $true; awaitPromise = $true }
    if ($null -eq $r) { throw "нет ответа CDP на: $expr" }
    if ($r.result.exceptionDetails) {
        $det = $r.result.exceptionDetails
        $msg = $det.text
        if ($det.exception -and $det.exception.description) { $msg = $det.exception.description }
        throw ('ошибка JS: ' + $msg + ' [' + $expr + ']')
    }
    return $r.result.result.value
}

Stop-Nodes
if (Test-Path $share) { Remove-Item -Recurse -Force $share }
New-Item -ItemType Directory -Force -Path $share | Out-Null
Remove-Item -Force $cfg -ErrorAction SilentlyContinue

Write-Host '--- запуск узла ---'
$p = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -ArgumentList @(
    '--share', "Общая=$share", '--port', '8103', '--ftp-port', '2133', '--udp-port', '45572',
    '--name', 'Node-Places', '--config', $cfg, '--no-ui', '--no-sync')

$edge = $null
$ws = $null
try {
    $deadline = (Get-Date).AddSeconds(20)
    $ok = $false
    while ((Get-Date) -lt $deadline) {
        try { Invoke-RestMethod "$base/api/status" -TimeoutSec 2 | Out-Null; $ok = $true; break } catch { Start-Sleep -Milliseconds 400 }
    }
    if (-not $ok) { throw 'узел не отвечает' }

    Write-Host '--- API /api/browse: список быстрых переходов ---'
    $b = Invoke-RestMethod "$base/api/browse"
    Write-Host ("  стартовый путь: " + $b.path)
    foreach ($pl in $b.places) {
        $exists = Test-Path -LiteralPath $pl.path
        Write-Host ("  " + $pl.label + " -> " + $pl.path + " (существует: " + $exists + ")")
        if (-not $exists) { throw ("быстрый переход ведёт в несуществующую папку: " + $pl.path) }
    }
    $labels = @($b.places | ForEach-Object { $_.label })
    foreach ($need in @('Документы', 'Рабочий стол', 'Изображения', 'Загрузки', 'Музыка', 'Видео')) {
        if ($labels -notcontains $need) { throw ("нет быстрого перехода «" + $need + "»") }
    }
    Write-Host '  обязательные переходы на месте: OK'

    Write-Host '--- API /api/browse?drives=1: все диски ---'
    $d = Invoke-RestMethod "$base/api/browse?drives=1"
    Write-Host ("  путь: '" + $d.path + "', диски: " + ($d.drives -join ', '))
    if ($d.drives.Count -lt 1) { throw 'список дисков пуст' }

    Write-Host '--- окно выбора папки в браузере (headless Edge + CDP) ---'
    $edgePath = Get-EdgePath
    if (-not $edgePath) { throw 'Edge не найден — нечем проверить интерфейс (нужен Microsoft Edge)' }
    if (Test-Path $edgeTmp) { Remove-Item -Recurse -Force $edgeTmp }
    $edge = Start-Process -FilePath $edgePath -PassThru -WindowStyle Hidden -ArgumentList @(
        '--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
        '--remote-debugging-port=9231', "--user-data-dir=$edgeTmp", "$base/")

    $wsUrl = $null
    $deadline = (Get-Date).AddSeconds(25)
    while ((Get-Date) -lt $deadline) {
        try {
            $targets = Invoke-RestMethod 'http://127.0.0.1:9231/json' -TimeoutSec 2
            $page = $targets | Where-Object { $_.type -eq 'page' } | Select-Object -First 1
            if ($page) { $wsUrl = $page.webSocketDebuggerUrl; break }
        } catch { }
        Start-Sleep -Milliseconds 400
    }
    if (-not $wsUrl) { throw 'CDP недоступен' }

    $ws = New-Object System.Net.WebSockets.ClientWebSocket
    $ws.ConnectAsync([Uri]$wsUrl, [System.Threading.CancellationToken]::None).GetAwaiter().GetResult() | Out-Null

    $deadline = (Get-Date).AddSeconds(20)
    $shares = '0'
    while ((Get-Date) -lt $deadline) {
        try { $shares = Eval-Js $ws 1 "(document.getElementById('shareCount')||{}).textContent || '0'" } catch { $shares = '0' }
        if ($shares -eq '1') { break }
        Start-Sleep -Milliseconds 400
    }
    Write-Host ("  список сетевых папок отрисован: " + $shares)
    if ($shares -ne '1') { throw 'интерфейс не отрисовал список папок (ошибка JS?)' }

    $null = Eval-Js $ws 2 "document.getElementById('btnAddShare').click(); 'ok'"
    Start-Sleep -Milliseconds 900
    $open = Eval-Js $ws 3 "!document.getElementById('modalRoot').hidden"
    Write-Host ("  окно «Добавить папку» открылось: " + $open)
    if (-not $open) { throw 'окно выбора папки не открылось' }

    $chips = Eval-Js $ws 4 "Array.from(document.querySelectorAll('#browsePlaces .place span')).map(function(e){return e.textContent}).join('|')"
    Write-Host ("  быстрые переходы в окне: " + $chips)
    foreach ($need in @('Документы', 'Рабочий стол', 'Изображения', 'Все диски')) {
        if ($chips -notlike ('*' + $need + '*')) { throw ("в окне нет кнопки «" + $need + "»") }
    }

    $nameVal = Eval-Js $ws 5 "document.getElementById('browseName').value"
    Write-Host ("  имя папки в сети по умолчанию: " + $nameVal)
    $hint = Eval-Js $ws 6 "document.getElementById('browseHint').textContent"
    Write-Host ("  подсказка о синхронизации: " + $hint)
    if (-not $hint) { throw 'нет подсказки о совпадении имён' }

    Write-Host '--- переход «Документы» внутри окна ---'
    $null = Eval-Js $ws 7 "Array.from(document.querySelectorAll('#browsePlaces .place')).filter(function(b){return b.textContent.indexOf('Документы')>=0})[0].click(); 'ok'"
    Start-Sleep -Milliseconds 900
    $docsPath = ($b.places | Where-Object { $_.label -eq 'Документы' } | Select-Object -First 1).path
    $cur = Eval-Js $ws 8 "document.getElementById('browseCurrent').textContent"
    $nameVal = Eval-Js $ws 9 "document.getElementById('browseName').value"
    Write-Host ("  текущий путь: " + $cur)
    Write-Host ("  имя папки в сети: " + $nameVal)
    if ($cur -ne $docsPath) { throw 'переход в «Документы» не сработал' }
    if (-not $nameVal) { throw 'имя папки в сети не подставилось' }

    Write-Host '--- переход «Все диски» ---'
    $null = Eval-Js $ws 10 "Array.from(document.querySelectorAll('#browsePlaces .place')).filter(function(b){return b.textContent.indexOf('Все диски')>=0})[0].click(); 'ok'"
    Start-Sleep -Milliseconds 900
    $list = Eval-Js $ws 11 "Array.from(document.querySelectorAll('#browseList .dir')).map(function(e){return e.textContent}).join('|')"
    Write-Host ("  список дисков в окне: " + $list)
    if ($list -notmatch ':\\') { throw 'в окне не показаны диски' }

    Write-Host '--- второй узел: подсказки о парах папок ---'
    $shareB = Join-Path $t 'shareB'
    if (Test-Path $shareB) { Remove-Item -Recurse -Force $shareB }
    New-Item -ItemType Directory -Force -Path $shareB | Out-Null
    $cfgB = Join-Path $t 'places-b.ini'
    Remove-Item -Force $cfgB -ErrorAction SilentlyContinue
    $pb = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -ArgumentList @(
        '--share', "Фото=$shareB", '--port', '8104', '--ftp-port', '2134', '--udp-port', '45572',
        '--name', 'Node-Photo', '--config', $cfgB, '--no-ui', '--no-sync')

    $peer = $null
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        $st = Invoke-RestMethod "$base/api/status"
        $peer = $st.peers | Where-Object { $_.name -eq 'Node-Photo' -and $_.online } | Select-Object -First 1
        if ($peer -and ($peer.shares -contains 'Фото')) { break }
        Start-Sleep -Milliseconds 500
    }
    Write-Host ("  узел виден, его папки: " + ($peer.shares -join ', '))
    if (-not ($peer.shares -contains 'Фото')) { throw 'имена папок соседа не подтянулись' }

    Start-Sleep -Seconds 3  # интерфейс опрашивает /api/status раз в 2 с
    $null = Eval-Js $ws 12 "document.getElementById('btnAddShare').click(); 'ok'"
    Start-Sleep -Milliseconds 800
    $matches = Eval-Js $ws 13 "document.getElementById('browseMatches').textContent"
    Write-Host ("  подсказка соседа в окне: " + $matches)
    if ($matches -notlike '*Фото*') { throw 'в окне нет имени папки соседа' }

    $hintWarn = Eval-Js $ws 14 "document.getElementById('browseHint').className"
    Write-Host ("  имя «Общая» не совпадает, класс подсказки: " + $hintWarn)
    if ($hintWarn -notlike '*warn*') { throw 'нет предупреждения о несовпадении имён' }

    $null = Eval-Js $ws 15 "Array.from(document.querySelectorAll('#browseMatches .place')).filter(function(b){return b.textContent.indexOf('Фото')>=0})[0].click(); 'ok'"
    Start-Sleep -Milliseconds 600
    # Щелчок по папке соседа подставляет её имя как КЛЮЧ синхронизации: своё имя
    # папки можно оставить любым (так описано в README).
    $pickedKey = Eval-Js $ws 16 "document.getElementById('browseKey').value"
    $pickedName = Eval-Js $ws 17 "document.getElementById('browseName').value"
    $hintOk = Eval-Js $ws 18 "document.getElementById('browseHint').className"
    Write-Host ("  после выбора папки соседа: ключ='" + $pickedKey + "', имя='" + $pickedName + "', класс подсказки: " + $hintOk)
    if ($pickedKey -ne 'Фото') { throw 'кнопка с именем соседа не подставила ключ синхронизации' }
    if ($hintOk -notlike '*ok*') { throw 'нет подтверждения, что папки синхронизируются' }

    $null = Eval-Js $ws 19 "document.getElementById('btnBrowseCancel').click(); 'ok'"
    Start-Sleep -Milliseconds 500
    $badge = Eval-Js $ws 20 "(document.querySelector('#shareList .shareinfo .nopair') || {}).textContent || ''"
    Write-Host ("  отметка в списке папок: " + $badge)
    if ($badge -notlike '*нет пары*') { throw 'у папки без пары нет отметки' }

    Write-Host 'ИТОГ: быстрый переход, имя сетевой папки и подсказки о парах работают'
}
finally {
    if ($ws) { try { $ws.Dispose() } catch { } }
    if ($edge -and -not $edge.HasExited) { Stop-Process -Id $edge.Id -Force -ErrorAction SilentlyContinue }
    Stop-Nodes
    Remove-Item -Recurse -Force $edgeTmp -ErrorAction SilentlyContinue
    Remove-Item -Force $cfg, (Join-Path $t 'places-b.ini') -ErrorAction SilentlyContinue
}
