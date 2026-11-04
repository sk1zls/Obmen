$ErrorActionPreference = 'Stop'

$hostIp = '127.0.0.1'
$port = 2122
$sharedDir = 'G:\obmen\test\share1'
$shareName = 'share1'

function New-Control {
    param([string]$ip, [int]$p)
    $c = [System.Net.Sockets.TcpClient]::new()
    $c.Connect($ip, $p)
    return $c
}

$ctrl = New-Control $hostIp $port
$stream = $ctrl.GetStream()
$reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8)
$writer = [System.IO.StreamWriter]::new($stream, [System.Text.UTF8Encoding]::new($false))
$writer.NewLine = "`r`n"
$writer.AutoFlush = $true

function Send([string]$line) { $writer.WriteLine($line); return $reader.ReadLine() }
function Show($label, $reply) { Write-Host ("{0,-16} {1}" -f $label, $reply) }

Show 'greeting' $reader.ReadLine()
Show 'USER' (Send 'USER anonymous')
Show 'PASS' (Send 'PASS test@example.com')
Show 'SYST' (Send 'SYST')
Show 'PWD' (Send 'PWD')
Show 'TYPE I' (Send 'TYPE I')

# Верхний уровень FTP — список сетевых папок; заходим в общую папку.
Show 'CWD share' (Send "CWD $shareName")
Show 'PWD' (Send 'PWD')

function Enter-Passive {
    $r = Send 'PASV'
    $m = [regex]::Match($r, '\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)')
    if (-not $m.Success) { throw "Нет ответа PASV: $r" }
    $ip = "$($m.Groups[1].Value).$($m.Groups[2].Value).$($m.Groups[3].Value).$($m.Groups[4].Value)"
    $dp = [int]$m.Groups[5].Value * 256 + [int]$m.Groups[6].Value
    $d = [System.Net.Sockets.TcpClient]::new()
    $d.Connect($ip, $dp)
    return @{ Reply = $r; Client = $d }
}

Write-Host '--- LIST ---'
$p = Enter-Passive
Show 'PASV' $p.Reply
$data = $p.Client.GetStream()
Show 'LIST' (Send 'LIST')
$ms = [System.IO.MemoryStream]::new()
$data.CopyTo($ms)
$p.Client.Close()
$listing = [System.Text.Encoding]::UTF8.GetString($ms.ToArray())
Write-Host "listing bytes: $($ms.Length)"
Write-Host $listing
Show 'after LIST' $reader.ReadLine()

Write-Host '--- STOR (upload) ---'
$payload = [byte[]]::new(4096)
for ($i = 0; $i -lt $payload.Length; $i++) { $payload[$i] = [byte]($i % 251) }
$p = Enter-Passive
Show 'PASV' $p.Reply
$data = $p.Client.GetStream()
Show 'STOR' (Send 'STOR ftp-test.bin')
$data.Write($payload, 0, $payload.Length)
$data.Flush()
$p.Client.Close()
Show 'after STOR' $reader.ReadLine()

$onDisk = Join-Path $sharedDir 'ftp-test.bin'
if (-not (Test-Path $onDisk)) { throw "Файл не появился на диске: $onDisk" }
$diskBytes = [System.IO.File]::ReadAllBytes($onDisk)
$same = ((($payload -join ',') -eq ($diskBytes -join ',')))
Write-Host "на диске: $($diskBytes.Length) байт, совпадает=$same"

Write-Host '--- SIZE / MDTM ---'
Show 'SIZE' (Send 'SIZE ftp-test.bin')
Show 'MDTM' (Send 'MDTM ftp-test.bin')

Write-Host '--- RETR (download) ---'
$p = Enter-Passive
Show 'PASV' $p.Reply
$data = $p.Client.GetStream()
Show 'RETR' (Send 'RETR ftp-test.bin')
$ms2 = [System.IO.MemoryStream]::new()
$data.CopyTo($ms2)
$p.Client.Close()
$got = $ms2.ToArray()
$same2 = ((($payload -join ',') -eq ($got -join ',')))
Write-Host "получено: $($got.Length) байт, совпадает=$same2"
Show 'after RETR' $reader.ReadLine()

Write-Host '--- MKD / CWD / PWD / CDUP ---'
Show 'MKD' (Send 'MKD ftpdir')
Show 'CWD' (Send 'CWD ftpdir')
Show 'PWD' (Send 'PWD')
Show 'CDUP' (Send 'CDUP')
Show 'RMD' (Send 'RMD ftpdir')

Write-Host '--- DELE / ошибки ---'
Show 'DELE' (Send 'DELE ftp-test.bin')
Show 'RETR missing' (Send 'RETR ftp-test.bin')
Show 'CWD missing' (Send 'CWD nope')
Show 'BADCMD' (Send 'FOOBAR')
Show 'QUIT' (Send 'QUIT')
$ctrl.Close()

$exists = Test-Path $onDisk
Write-Host "файл удалён с диска: $(-not $exists)"
Write-Host 'ALL FTP TESTS DONE'
