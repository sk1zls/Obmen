# Встроенный FTP-сервер (PASV): вход, CWD по сетевым папкам, LIST, STOR, RETR,
# MKD/RMD, DELE, коды ошибок. Узел поднимается сам.
. (Join-Path $PSScriptRoot 'common.ps1')

Stop-AllTestNodes
$shareDir = New-TestShare 'ftp-share'
$node = Start-TestNode -HttpPort 8091 -FtpPort 2122 -UdpPort 45552 -Name 'Node-FTP' `
    -Shares @("share1=$shareDir") -Interval 3

$hostIp = '127.0.0.1'
$port = 2122
$shareName = 'share1'

function New-Control {
    param([string]$ip, [int]$p)
    $c = [System.Net.Sockets.TcpClient]::new()
    $c.Connect($ip, $p)
    return $c
}

try {
    $ctrl = New-Control $hostIp $port
    $stream = $ctrl.GetStream()
    $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8)
    $writer = [System.IO.StreamWriter]::new($stream, [System.Text.UTF8Encoding]::new($false))
    $writer.NewLine = "`r`n"
    $writer.AutoFlush = $true

    function Send([string]$line) { $writer.WriteLine($line); return $reader.ReadLine() }
    function Show($label, $reply) { Write-Host ("{0,-16} {1}" -f $label, $reply) }

    $greeting = $reader.ReadLine()
    Show 'greeting' $greeting
    Assert-True ($greeting -match '^220') 'FTP не поздоровался кодом 220'

    $user = Send 'USER anonymous'
    Show 'USER' $user
    Assert-True ($user -match '^(331|230)') 'USER отклонён'
    $pass = Send 'PASS test@example.com'
    Show 'PASS' $pass
    Assert-True ($pass -match '^230') 'PASS не принят'
    Show 'SYST' (Send 'SYST')
    $pwdRoot = Send 'PWD'
    Show 'PWD' $pwdRoot
    Assert-True ($pwdRoot -match '^257') 'PWD не вернул 257'
    Show 'TYPE I' (Send 'TYPE I')

    # Верхний уровень FTP — список сетевых папок; заходим в общую папку.
    $cwd = Send "CWD $shareName"
    Show 'CWD share' $cwd
    Assert-True ($cwd -match '^250') 'CWD в сетевую папку не выполнен'
    $pwd = Send 'PWD'
    Show 'PWD' $pwd
    Assert-True ($pwd -match $shareName) 'PWD не показывает сетевую папку'

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
    $lst = Send 'LIST'
    Show 'LIST' $lst
    Assert-True ($lst -match '^150') 'LIST не начал передачу'
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
    $stor = Send 'STOR ftp-test.bin'
    Show 'STOR' $stor
    Assert-True ($stor -match '^150') 'STOR не начал передачу'
    $data.Write($payload, 0, $payload.Length)
    $data.Flush()
    $p.Client.Close()
    $after = $reader.ReadLine()
    Show 'after STOR' $after
    Assert-True ($after -match '^226') 'STOR не завершился кодом 226'

    $onDisk = Join-Path $shareDir 'ftp-test.bin'
    Assert-True (Test-Path -LiteralPath $onDisk) "файл не появился на диске: $onDisk"
    $diskBytes = [System.IO.File]::ReadAllBytes($onDisk)
    $same = ((($payload -join ',') -eq ($diskBytes -join ',')))
    Write-Host "на диске: $($diskBytes.Length) байт, совпадает=$same"
    Assert-True $same 'содержимое на диске отличается от отправленного'

    Write-Host '--- SIZE / MDTM ---'
    $size = Send 'SIZE ftp-test.bin'
    Show 'SIZE' $size
    Assert-True ($size -match '^213 4096') 'SIZE вернул неверный размер'
    $mdtm = Send 'MDTM ftp-test.bin'
    Show 'MDTM' $mdtm
    Assert-True ($mdtm -match '^213') 'MDTM не вернул 213'

    Write-Host '--- RETR (download) ---'
    $p = Enter-Passive
    Show 'PASV' $p.Reply
    $data = $p.Client.GetStream()
    $retr = Send 'RETR ftp-test.bin'
    Show 'RETR' $retr
    Assert-True ($retr -match '^150') 'RETR не начал передачу'
    $ms2 = [System.IO.MemoryStream]::new()
    $data.CopyTo($ms2)
    $p.Client.Close()
    $got = $ms2.ToArray()
    $same2 = ((($payload -join ',') -eq ($got -join ',')))
    Write-Host "получено: $($got.Length) байт, совпадает=$same2"
    Assert-True $same2 'скачанный по FTP файл отличается'
    Show 'after RETR' $reader.ReadLine()

    Write-Host '--- MKD / CWD / PWD / CDUP ---'
    $mkd = Send 'MKD ftpdir'
    Show 'MKD' $mkd
    Assert-True ($mkd -match '^257') 'MKD не создал папку'
    $cwd2 = Send 'CWD ftpdir'
    Show 'CWD' $cwd2
    Assert-True ($cwd2 -match '^250') 'CWD во вложенную папку не выполнен'
    Show 'PWD' (Send 'PWD')
    $cdup = Send 'CDUP'
    Show 'CDUP' $cdup
    Assert-True ($cdup -match '^250') 'CDUP не выполнен'
    $rmd = Send 'RMD ftpdir'
    Show 'RMD' $rmd
    Assert-True ($rmd -match '^250') 'RMD не удалил папку'

    Write-Host '--- DELE / ошибки ---'
    $dele = Send 'DELE ftp-test.bin'
    Show 'DELE' $dele
    Assert-True ($dele -match '^250') 'DELE не удалил файл'
    $miss = Send 'RETR ftp-test.bin'
    Show 'RETR missing' $miss
    Assert-True ($miss -match '^550') 'RETR отсутствующего файла не 550'
    $cwdBad = Send 'CWD nope'
    Show 'CWD missing' $cwdBad
    Assert-True ($cwdBad -match '^550') 'CWD несуществующей папки не 550'
    $bad = Send 'FOOBAR'
    Show 'BADCMD' $bad
    Assert-True ($bad -match '^5\d\d') 'неизвестная команда не 5xx'
    $quit = Send 'QUIT'
    Show 'QUIT' $quit
    Assert-True ($quit -match '^221') 'QUIT не вернул 221'
    $ctrl.Close()

    $exists = Test-Path -LiteralPath $onDisk
    Write-Host "файл удалён с диска: $(-not $exists)"
    Assert-True (-not $exists) 'файл остался на диске после DELE'
}
finally {
    Stop-TestNode $node
}

Write-Host 'ALL FTP TESTS DONE'
