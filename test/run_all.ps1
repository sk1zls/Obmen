# Прогон всех сценариев из test\*.ps1 подряд.
#
#   pwsh -NoProfile -File test\run_all.ps1              # всё, кроме оконных проверок
#   pwsh -NoProfile -File test\run_all.ps1 -IncludeUi    # + test_ui.ps1 (нужен рабочий стол)
#   pwsh -NoProfile -File test\run_all.ps1 -Filter http  # только совпадающие по имени
#
# Каждый сценарий поднимает свои узлы на своих портах и убирает их за собой,
# поэтому порядок запуска и наличие других копий программы на машине не важны.
param(
    [string]$Filter = '',
    [switch]$IncludeUi
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$null = Get-ObmenExe   # понятная ошибка, если программа ещё не собрана

$scripts = Get-ChildItem -Path $PSScriptRoot -Filter 'test_*.ps1' | Sort-Object Name
if (-not $IncludeUi) { $scripts = $scripts | Where-Object { $_.Name -ne 'test_ui.ps1' } }
if ($Filter) { $scripts = $scripts | Where-Object { $_.Name -like "*$Filter*" } }
if (-not $scripts) { throw "нет сценариев по фильтру '$Filter'" }

$passed = @()
$failed = @()
foreach ($s in $scripts) {
    Write-Host ''
    Write-Host ("=== " + $s.Name + " ===") -ForegroundColor Cyan
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $s.FullName
    $code = $LASTEXITCODE
    $sw.Stop()
    $secs = [math]::Round($sw.Elapsed.TotalSeconds)
    if ($code -eq 0) {
        $passed += $s.Name
        Write-Host ("  OK, $secs c") -ForegroundColor Green
    } else {
        $failed += $s.Name
        Write-Host ("  ОШИБКА, код $code, $secs c") -ForegroundColor Red
    }
}

Write-Host ''
Write-Host ("Пройдено: {0} из {1}" -f $passed.Count, ($passed.Count + $failed.Count))
if ($failed.Count) {
    Write-Host ("Не прошли: " + ($failed -join ', ')) -ForegroundColor Red
    exit 1
}
if (-not $IncludeUi) { Write-Host 'test_ui.ps1 не запускался (нужен рабочий стол): -IncludeUi' }
Write-Host 'ВСЕ СЦЕНАРИИ ПРОЙДЕНЫ' -ForegroundColor Green
