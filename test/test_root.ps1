# Смена пути сетевой папки через API и запись в obmen.ini.
$ErrorActionPreference = 'Stop'
$A = 'http://127.0.0.1:8091'
$ini = Join-Path $PSScriptRoot 'a.ini'

Write-Host '--- 1. текущее состояние ---'
$st = Invoke-RestMethod "$A/api/status"
Write-Host ("папки: " + (($st.shares | ForEach-Object { $_.name + ' → ' + $_.path }) -join '; '))
if (-not $st.shares.Count) { throw 'у узла нет сетевых папок' }
$share = $st.shares[0].name
$oldPath = $st.shares[0].path
Invoke-RestMethod -Uri "$A/api/sync" -Method Post -Body '{"enabled":false}' -ContentType 'application/json' | Out-Null
Write-Host 'синхронизация выключена на время теста'

Write-Host '--- 2. переключение пути на share2 через API ---'
$body = @{ share = $share; path = 'G:\obmen\test\share2' } | ConvertTo-Json -Compress
$r = Invoke-RestMethod -Uri "$A/api/setroot" -Method Post -Body $body -ContentType 'application/json'
Write-Host "ok=$($r.ok)"
$st2 = Invoke-RestMethod "$A/api/status"
$now = ($st2.shares | Where-Object { $_.name -eq $share }).path
Write-Host "новый путь «$share»: $now"
if ($now -notlike '*share2') { throw 'путь папки не изменился' }

$files = (Invoke-RestMethod ("$A/api/files?share=" + [uri]::EscapeDataString($share) + "&path=")).entries
Write-Host ("видно записей: " + $files.Count + ": " + (($files | ForEach-Object { $_.name }) -join ', '))

Write-Host '--- 3. конфиг на диске ---'
if (Test-Path $ini) {
    $cfg = Get-Content $ini -Raw
    ($cfg -split "`n" | Where-Object { $_ -match '^(share|name|http_port|sync_enabled)=' }) -join ' | ' | Write-Host
    if ($cfg -notmatch 'share=') { throw 'в obmen.ini нет строк share=' }
} else {
    Write-Host "  (файла $ini нет — узел запущен без --config)"
}

Write-Host '--- 4. некорректный путь ---'
$bad = Invoke-WebRequest -Uri "$A/api/setroot" -Method Post -Body (@{ share = $share; path = 'G:\obmen\test\a.ini' } | ConvertTo-Json -Compress) -ContentType 'application/json' -SkipHttpErrorCheck
Write-Host "файл вместо папки -> $($bad.StatusCode) $($bad.Content)"
$empty = Invoke-WebRequest -Uri "$A/api/setroot" -Method Post -Body (@{ share = $share; path = '' } | ConvertTo-Json -Compress) -ContentType 'application/json' -SkipHttpErrorCheck
Write-Host "пустой путь -> $($empty.StatusCode) $($empty.Content)"
$traversal = Invoke-WebRequest -Uri "$A/api/files?path=../.." -SkipHttpErrorCheck
Write-Host "выход из папки -> $($traversal.StatusCode) $($traversal.Content)"

Write-Host '--- 5. возврат пути ---'
$back = @{ share = $share; path = $oldPath } | ConvertTo-Json -Compress
Invoke-RestMethod -Uri "$A/api/setroot" -Method Post -Body $back -ContentType 'application/json' | Out-Null
$st3 = Invoke-RestMethod "$A/api/status"
Write-Host ("путь восстановлен: " + ($st3.shares | Where-Object { $_.name -eq $share }).path)
Invoke-RestMethod -Uri "$A/api/sync" -Method Post -Body '{"enabled":true}' -ContentType 'application/json' | Out-Null
Write-Host 'ALL ROOT/PATH TESTS DONE'
