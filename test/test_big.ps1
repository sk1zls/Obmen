$ErrorActionPreference = 'Stop'
$A = 'http://127.0.0.1:8091'
$tmp = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\build\tmp'))
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
function enc([string]$s) { [System.Uri]::EscapeDataString($s) }

Write-Host '--- 1. файл 20 МБ: загрузка/скачивание/целостность ---'
$big = Join-Path $tmp 'big.bin'
$fs = [System.IO.File]::Create($big)
$rnd = [System.Random]::new(42)
$chunk = [byte[]]::new(1MB)
for ($i = 0; $i -lt 20; $i++) {
    $rnd.NextBytes($chunk)
    $fs.Write($chunk, 0, $chunk.Length)
}
$fs.Close()
$hashSrc = (Get-FileHash $big -Algorithm SHA256).Hash
"исходный файл: $((Get-Item $big).Length) байт, sha256=$($hashSrc.Substring(0,16))..."

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$up = Invoke-RestMethod -Uri "$A/api/upload?path=&name=big-test.bin" -Method Post -InFile $big -ContentType 'application/octet-stream' -TimeoutSec 120
$sw.Stop()
"загрузка: ok=$($up.ok) за $([math]::Round($sw.Elapsed.TotalSeconds,2)) с ($([math]::Round(20/$sw.Elapsed.TotalSeconds,1)) МБ/с)"

$back = Join-Path $tmp 'big-back.bin'
$sw.Restart()
Invoke-WebRequest -Uri "$A/api/download?path=big-test.bin" -OutFile $back -TimeoutSec 120
$sw.Stop()
$hashDst = (Get-FileHash $back -Algorithm SHA256).Hash
"скачивание: $((Get-Item $back).Length) байт за $([math]::Round($sw.Elapsed.TotalSeconds,2)) с; sha256 совпадает=$($hashSrc -eq $hashDst)"

$entry = (Invoke-RestMethod "$A/api/files?path=").entries | Where-Object { $_.name -eq 'big-test.bin' }
"в листинге: name=$($entry.name) size=$($entry.size) mtime=$($entry.mtime)"

Write-Host '--- 2. имя файла со спецсимволами ---'
$oddName = 'файл (1) [x] & тест.txt'
$body = [System.Text.Encoding]::UTF8.GetBytes('спецсимволы в имени')
$up2 = Invoke-RestMethod -Uri "$A/api/upload?path=&name=$(enc $oddName)" -Method Post -Body $body -ContentType 'application/octet-stream'
"загрузка: ok=$($up2.ok)"
$list = (Invoke-RestMethod "$A/api/files?path=").entries
$found = $list | Where-Object { $_.name -eq $oddName }
"найдено в листинге: $([bool]$found) (rel=$($found.rel))"
$dl = Join-Path $tmp 'odd.txt'
Invoke-WebRequest -Uri "$A/api/download?path=$(enc $found.rel)" -OutFile $dl -TimeoutSec 30
"скачано и совпадает: $((Get-Content $dl -Raw) -eq 'спецсимволы в имени')"

Write-Host '--- 3. параллельные загрузки (4 x 2 МБ) ---'
Invoke-RestMethod -Uri "$A/api/mkdir?path=&name=parallel" -Method Post | Out-Null
$jobs = @()
for ($i = 1; $i -le 4; $i++) {
    $f = Join-Path $tmp "p$i.bin"
    $bytes = [byte[]]::new(2MB)
    [System.Random]::new($i).NextBytes($bytes)
    [System.IO.File]::WriteAllBytes($f, $bytes)
    $jobs += Start-Job -ScriptBlock {
        param($url, $file)
        Invoke-RestMethod -Uri $url -Method Post -InFile $file -ContentType 'application/octet-stream' -TimeoutSec 60
    } -ArgumentList "$A/api/upload?path=parallel&name=$([System.IO.Path]::GetFileName($f))", $f
}
$jobs | Wait-Job -Timeout 90 | Out-Null
$jobs | ForEach-Object { $r = Receive-Job $_; "job: ok=$($r.ok)" }
$jobs | Remove-Job -Force
$plist = (Invoke-RestMethod "$A/api/files?path=parallel").entries
"в папке parallel: $($plist.Count) файлов: " + (($plist | ForEach-Object { "$($_.name)($($_.size))" }) -join ', ')

Write-Host '--- 4. уборка ---'
$delNames = @('big-test.bin', $oddName)
foreach ($n in $delNames) { Invoke-RestMethod -Uri "$A/api/delete?path=$(enc $n)" -Method Post | Out-Null }
Invoke-RestMethod -Uri "$A/api/delete?path=parallel" -Method Post | Out-Null
$final = (Invoke-RestMethod "$A/api/files?path=").entries
"после уборки записей: $($final.Count): " + (($final | ForEach-Object { $_.name }) -join ', ')
Remove-Item $big, $back -ErrorAction SilentlyContinue
Write-Host 'ALL BIG/CONCURRENCY TESTS DONE'
