$ErrorActionPreference = 'Stop'
$base = 'http://127.0.0.1:8091'
function enc([string]$s) { [System.Uri]::EscapeDataString($s) }

Write-Host '--- status ---'
$st = Invoke-RestMethod "$base/api/status"
"node=$($st.node.name) id=$($st.node.id)"
"root=$($st.root)"
"ports: http=$($st.ports.http) ftp=$($st.ports.ftp) udp=$($st.ports.udp)"
"ips=$($st.ips -join ',') sync=$($st.sync.enabled) peers=$($st.peers.Count)"

Write-Host '--- gui static ---'
$r = Invoke-WebRequest "$base/" -UseBasicParsing
"GET /       -> $($r.StatusCode) $($r.Headers['Content-Type']) len=$($r.RawContentLength)"
$css = Invoke-WebRequest "$base/app.css" -UseBasicParsing
"GET /app.css -> $($css.StatusCode) len=$($css.RawContentLength)"
$js = Invoke-WebRequest "$base/app.js" -UseBasicParsing
"GET /app.js  -> $($js.StatusCode) len=$($js.RawContentLength)"

Write-Host '--- files listing ---'
$f = Invoke-RestMethod "$base/api/files?path="
"entries=$($f.entries.Count): " + (($f.entries | ForEach-Object { "$($_.name) [$($_.size)]" }) -join ' | ')

Write-Host '--- upload (кириллица в имени) ---'
$name = 'тест-загрузка.txt'
$payload = [System.Text.Encoding]::UTF8.GetBytes("строка 1`nстрока 2 - кириллица")
$up = Invoke-RestMethod -Uri "$base/api/upload?path=&name=$(enc $name)" -Method Post -Body $payload -ContentType 'application/octet-stream'
"upload ok=$($up.ok)"
$f2 = Invoke-RestMethod "$base/api/files?path="
$entry = $f2.entries | Where-Object { $_.name -eq $name }
"entry: name=$($entry.name) size=$($entry.size) dir=$($entry.isDir)"

Write-Host '--- nested folder + upload inside ---'
$mk = Invoke-RestMethod -Uri "$base/api/mkdir?path=&name=$(enc 'вложенная')" -Method Post
"mkdir ok=$($mk.ok)"
$up2 = Invoke-RestMethod -Uri "$base/api/upload?path=$(enc 'вложенная')&name=inner.bin" -Method Post -Body ([byte[]](1,2,3,4,5,6,7,8)) -ContentType 'application/octet-stream'
$f3 = Invoke-RestMethod "$base/api/files?path=$(enc 'вложенная')"
"inner upload ok=$($up2.ok) entries=$($f3.entries.Count) first=$($f3.entries[0].name) rel=$($f3.entries[0].rel)"

Write-Host '--- download + byte compare ---'
$out = 'G:\obmen\test\dl.txt'
Invoke-WebRequest -Uri "$base/api/download?path=$(enc $name)" -OutFile $out -UseBasicParsing
$got = [System.IO.File]::ReadAllBytes($out)
$same = (($payload -join ',') -eq ($got -join ','))
"downloaded=$($got.Length) bytes identical=$same"

Write-Host '--- manifest ---'
$man = Invoke-RestMethod "$base/api/manifest"
"manifest entries=$($man.Count): " + (($man | ForEach-Object { "$($_.p)[$($_.s)]" }) -join ' | ')

Write-Host '--- security: path traversal ---'
$tr = Invoke-WebRequest "$base/api/download?path=..%2F..%2Fa.ini" -SkipHttpErrorCheck
"traversal ../.. -> $($tr.StatusCode) $($tr.Content)"
$tr2 = Invoke-WebRequest "$base/api/download?path=C%3A%5CWindows%5Cwin.ini" -SkipHttpErrorCheck
"absolute C:\... -> $($tr2.StatusCode) $($tr2.Content)"
$tr3 = Invoke-WebRequest "$base/api/nope" -SkipHttpErrorCheck
"unknown route -> $($tr3.StatusCode)"

Write-Host '--- browse ---'
$br = Invoke-RestMethod "$base/api/browse?path="
"browse path=$($br.path) drives=$($br.drives -join ',') dirs=$($br.dirs.Count)"

Write-Host '--- sync toggle ---'
$tg = Invoke-RestMethod -Uri "$base/api/sync" -Method Post -Body '{"enabled":false}' -ContentType 'application/json'
"toggle off ok=$($tg.ok)"
$st2 = Invoke-RestMethod "$base/api/status"
"sync enabled now = $($st2.sync.enabled)"
$tg2 = Invoke-RestMethod -Uri "$base/api/sync" -Method Post -Body '{"enabled":true}' -ContentType 'application/json'
$st3 = Invoke-RestMethod "$base/api/status"
"sync enabled now = $($st3.sync.enabled)"

Write-Host '--- delete ---'
$del = Invoke-RestMethod -Uri "$base/api/delete?path=$(enc $name)" -Method Post
"delete ok=$($del.ok)"
$f4 = Invoke-RestMethod "$base/api/files?path="
"entries after delete=$($f4.entries.Count)"

Write-Host 'ALL HTTP TESTS DONE'
