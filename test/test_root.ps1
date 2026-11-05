# Смена пути сетевой папки через API и запись в obmen.ini.
# Узел поднимается сам со своим файлом настроек.
. (Join-Path $PSScriptRoot 'common.ps1')

Stop-AllTestNodes
$shareDir = New-TestShare 'root-share'
$otherDir = New-TestShare 'root-share2'
$node = Start-TestNode -HttpPort 8091 -UdpPort 45555 -Name 'Node-ROOT' `
    -Shares @("Общая=$shareDir") -Interval 3
$A = $node.Base
$ini = $node.Config

try {
    Write-Host '--- 1. текущее состояние ---'
    $st = Invoke-RestMethod "$A/api/status"
    Write-Host ("папки: " + ((@($st.shares) | ForEach-Object { $_.name + ' → ' + $_.path }) -join '; '))
    Assert-True (@($st.shares).Count -eq 1) 'у узла должна быть одна сетевая папка'
    $share = $st.shares[0].name
    $oldPath = $st.shares[0].path
    $null = Invoke-RestMethod -Uri "$A/api/sync" -Method Post -Body '{"enabled":false}' -ContentType 'application/json'
    Write-Host 'синхронизация выключена на время теста'

    Write-Host '--- 2. переключение пути через API ---'
    $body = @{ share = $share; path = $otherDir } | ConvertTo-Json -Compress
    $r = Invoke-RestMethod -Uri "$A/api/setroot" -Method Post -Body $body -ContentType 'application/json'
    Write-Host "ok=$($r.ok)"
    Assert-True ([bool]$r.ok) 'смена пути не подтверждена'
    $st2 = Invoke-RestMethod "$A/api/status"
    $now = ($st2.shares | Where-Object { $_.name -eq $share }).path
    Write-Host "новый путь «$share»: $now"
    Assert-True ($now -like '*root-share2*') 'путь папки не изменился'

    # В новой папке пусто, а старая папка на диске по-прежнему существует.
    $files = (Invoke-RestMethod ("$A/api/files?share=" + [uri]::EscapeDataString($share) + "&path=")).entries
    Write-Host ("видно записей: " + @($files).Count)
    Assert-True (@($files).Count -eq 0) 'новая папка должна быть пустой'
    Assert-True (Test-Path -LiteralPath $oldPath) 'старая папка исчезла с диска'

    Write-Host '--- 3. конфиг на диске ---'
    Assert-True (Test-Path -LiteralPath $ini) "узел не сохранил настройки в $ini"
    $cfg = Get-Content $ini -Raw
    (($cfg -split "`n") | Where-Object { $_ -match '^(share|name|http_port|sync_enabled)=' }) -join ' | ' | Write-Host
    Assert-True ($cfg -match 'share=') 'в obmen.ini нет строк share='
    Assert-True ($cfg -match 'root-share2') 'в obmen.ini не записан новый путь'

    Write-Host '--- 4. некорректный путь ---'
    $notADir = Join-Path $RepoRoot 'build\tmp\not-a-dir.ini'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $notADir) | Out-Null
    Set-Content -Path $notADir -Value 'это файл, а не папка' -Encoding UTF8
    $bad = Invoke-WebRequest -Uri "$A/api/setroot" -Method Post -Body (@{ share = $share; path = $notADir } | ConvertTo-Json -Compress) -ContentType 'application/json' -SkipHttpErrorCheck
    Write-Host "файл вместо папки -> $($bad.StatusCode) $($bad.Content)"
    Assert-True ($bad.StatusCode -eq 400) 'файл вместо папки не отклонён'
    $empty = Invoke-WebRequest -Uri "$A/api/setroot" -Method Post -Body (@{ share = $share; path = '' } | ConvertTo-Json -Compress) -ContentType 'application/json' -SkipHttpErrorCheck
    Write-Host "пустой путь -> $($empty.StatusCode) $($empty.Content)"
    Assert-True ($empty.StatusCode -eq 400) 'пустой путь не отклонён'
    $traversal = Invoke-WebRequest -Uri "$A/api/files?path=../.." -SkipHttpErrorCheck
    Write-Host "выход из папки -> $($traversal.StatusCode)"
    Assert-True ($traversal.StatusCode -in 400, 403, 404) 'выход из папки не заблокирован'

    Write-Host '--- 5. возврат пути ---'
    $back = @{ share = $share; path = $oldPath } | ConvertTo-Json -Compress
    $null = Invoke-RestMethod -Uri "$A/api/setroot" -Method Post -Body $back -ContentType 'application/json'
    $st3 = Invoke-RestMethod "$A/api/status"
    $restored = ($st3.shares | Where-Object { $_.name -eq $share }).path
    Write-Host ("путь восстановлен: " + $restored)
    Assert-True ($restored -eq $oldPath) 'путь не вернулся к исходному'
    $null = Invoke-RestMethod -Uri "$A/api/sync" -Method Post -Body '{"enabled":true}' -ContentType 'application/json'
    Remove-Item -LiteralPath $notADir -Force -ErrorAction SilentlyContinue
}
finally {
    Stop-TestNode $node
}

Write-Host 'ALL ROOT/PATH TESTS DONE'
