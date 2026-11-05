# HTTP API: статус, статика GUI, листинг, загрузка/скачивание, обход путей,
# переключатель синхронизации и удаление. Узел поднимается сам — заранее
# ничего запускать не нужно.
. (Join-Path $PSScriptRoot 'common.ps1')

Stop-AllTestNodes
$node = Start-TestNode -HttpPort 8091 -UdpPort 45551 -Name 'Node-HTTP' `
    -Shares @("Общая=$(New-TestShare 'http-share')") -Interval 3

try {
    $base = $node.Base
    function enc([string]$s) { [System.Uri]::EscapeDataString($s) }

    Write-Host '--- status ---'
    $st = Invoke-RestMethod "$base/api/status"
    "node=$($st.node.name) id=$($st.node.id)"
    "root=$($st.root)"
    "ports: http=$($st.ports.http) ftp=$($st.ports.ftp) udp=$($st.ports.udp)"
    "ips=$($st.ips -join ',') sync=$($st.sync.enabled) peers=$($st.peers.Count)"
    Assert-True ($st.ports.http -eq 8091) 'в статусе не тот HTTP-порт'
    Assert-True (@($st.shares).Count -eq 1) 'у узла должна быть одна сетевая папка'

    Write-Host '--- gui static ---'
    $r = Invoke-WebRequest "$base/" -UseBasicParsing
    "GET /       -> $($r.StatusCode) $($r.Headers['Content-Type']) len=$($r.RawContentLength)"
    Assert-True ($r.StatusCode -eq 200) 'GET / вернул не 200'
    Assert-True ($r.Content -match 'id="syncbar"') 'в HTML нет статус-строки синхронизации'
    $css = Invoke-WebRequest "$base/app.css" -UseBasicParsing
    "GET /app.css -> $($css.StatusCode) len=$($css.RawContentLength)"
    $js = Invoke-WebRequest "$base/app.js" -UseBasicParsing
    "GET /app.js  -> $($js.StatusCode) len=$($js.RawContentLength)"
    Assert-True ($js.Content -match 'function renderSyncBar') 'в app.js нет renderSyncBar'

    Write-Host '--- files listing ---'
    $f = Invoke-RestMethod "$base/api/files?path="
    "entries=$($f.entries.Count): " + (($f.entries | ForEach-Object { "$($_.name) [$($_.size)]" }) -join ' | ')

    Write-Host '--- upload (кириллица в имени) ---'
    $name = 'тест-загрузка.txt'
    $payload = [System.Text.Encoding]::UTF8.GetBytes("строка 1`nстрока 2 - кириллица")
    $up = Invoke-RestMethod -Uri "$base/api/upload?path=&name=$(enc $name)" -Method Post -Body $payload -ContentType 'application/octet-stream'
    "upload ok=$($up.ok)"
    Assert-True ([bool]$up.ok) 'загрузка не подтверждена'
    $f2 = Invoke-RestMethod "$base/api/files?path="
    $entry = $f2.entries | Where-Object { $_.name -eq $name }
    "entry: name=$($entry.name) size=$($entry.size) dir=$($entry.isDir)"
    Assert-True ([bool]$entry) 'загруженный файл не виден в листинге'
    Assert-True ($entry.size -eq $payload.Length) 'размер файла в листинге не совпал'

    Write-Host '--- nested folder + upload inside ---'
    $mk = Invoke-RestMethod -Uri "$base/api/mkdir?path=&name=$(enc 'вложенная')" -Method Post
    "mkdir ok=$($mk.ok)"
    $up2 = Invoke-RestMethod -Uri "$base/api/upload?path=$(enc 'вложенная')&name=inner.bin" -Method Post -Body ([byte[]](1, 2, 3, 4, 5, 6, 7, 8)) -ContentType 'application/octet-stream'
    $f3 = Invoke-RestMethod "$base/api/files?path=$(enc 'вложенная')"
    "inner upload ok=$($up2.ok) entries=$($f3.entries.Count) first=$($f3.entries[0].name) rel=$($f3.entries[0].rel)"
    Assert-True ([bool]$up2.ok -and $f3.entries.Count -eq 1) 'файл во вложенной папке не загрузился'

    Write-Host '--- download + byte compare ---'
    $out = Join-Path $TestDir 'dl.txt'
    Invoke-WebRequest -Uri "$base/api/download?path=$(enc $name)" -OutFile $out -UseBasicParsing
    $got = [System.IO.File]::ReadAllBytes($out)
    $same = (($payload -join ',') -eq ($got -join ','))
    "downloaded=$($got.Length) bytes identical=$same"
    Assert-True $same 'скачанный файл отличается от загруженного'
    Remove-Item -LiteralPath $out -Force -ErrorAction SilentlyContinue

    Write-Host '--- manifest ---'
    $man = Invoke-RestMethod "$base/api/manifest"
    "manifest entries=$(@($man).Count): " + ((@($man) | ForEach-Object { "$($_.p)[$($_.s)]" }) -join ' | ')
    Assert-True (@($man).Count -ge 2) 'в манифесте меньше двух файлов'

    Write-Host '--- security: path traversal ---'
    $tr = Invoke-WebRequest "$base/api/download?path=..%2F..%2Fa.ini" -SkipHttpErrorCheck
    "traversal ../.. -> $($tr.StatusCode)"
    Assert-True ($tr.StatusCode -in 400, 403, 404) 'выход из папки не заблокирован'
    $tr2 = Invoke-WebRequest "$base/api/download?path=C%3A%5CWindows%5Cwin.ini" -SkipHttpErrorCheck
    "absolute C:\... -> $($tr2.StatusCode)"
    Assert-True ($tr2.StatusCode -in 400, 403, 404) 'абсолютный путь не заблокирован'
    $tr3 = Invoke-WebRequest "$base/api/nope" -SkipHttpErrorCheck
    "unknown route -> $($tr3.StatusCode)"
    Assert-True ($tr3.StatusCode -eq 404) 'неизвестный маршрут не 404'

    Write-Host '--- browse ---'
    $br = Invoke-RestMethod "$base/api/browse?path="
    "browse path=$($br.path) drives=$($br.drives -join ',') dirs=$(@($br.dirs).Count)"

    Write-Host '--- sync toggle ---'
    $tg = Invoke-RestMethod -Uri "$base/api/sync" -Method Post -Body '{"enabled":false}' -ContentType 'application/json'
    "toggle off ok=$($tg.ok)"
    $st2 = Invoke-RestMethod "$base/api/status"
    "sync enabled now = $($st2.sync.enabled)"
    Assert-True ($st2.sync.enabled -eq $false) 'синхронизация не выключилась'
    $null = Invoke-RestMethod -Uri "$base/api/sync" -Method Post -Body '{"enabled":true}' -ContentType 'application/json'
    $st3 = Invoke-RestMethod "$base/api/status"
    "sync enabled now = $($st3.sync.enabled)"
    Assert-True ($st3.sync.enabled -eq $true) 'синхронизация не включилась'

    Write-Host '--- delete ---'
    $del = Invoke-RestMethod -Uri "$base/api/delete?path=$(enc $name)" -Method Post
    "delete ok=$($del.ok)"
    $f4 = Invoke-RestMethod "$base/api/files?path="
    "entries after delete=$($f4.entries.Count)"
    Assert-True (-not ($f4.entries | Where-Object { $_.name -eq $name })) 'файл не удалился'
}
finally {
    Stop-TestNode $node
}

Write-Host 'ALL HTTP TESTS DONE'
