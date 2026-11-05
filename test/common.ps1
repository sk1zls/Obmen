# Общие помощники для сценариев test\*.ps1.
#
# Подключается в начале теста:
#     . (Join-Path $PSScriptRoot 'common.ps1')
#
# Здесь нет ни одного абсолютного пути: всё считается от расположения
# репозитория, поэтому тесты работают на любой машине и в любой папке —
# достаточно собранного build\obmen.exe (build.bat).

$ErrorActionPreference = 'Stop'

$TestDir = $PSScriptRoot                          # ...\<repo>\test
$RepoRoot = Split-Path -Parent $TestDir           # ...\<repo>
$ExePath = Join-Path $RepoRoot 'build\obmen.exe'

# Путь к собранной программе с понятной ошибкой вместо «файл не найден».
function Get-ObmenExe {
    if (-not (Test-Path -LiteralPath $ExePath)) {
        throw "нет $ExePath — сначала соберите программу: build.bat"
    }
    return $ExePath
}

# Сетевая папка для теста: пересоздаётся с нуля, чтобы прогон был повторяемым.
function New-TestShare {
    param([Parameter(Mandatory)][string]$Name)
    $path = Join-Path $TestDir $Name
    if (Test-Path -LiteralPath $path) { Remove-Item -Recurse -Force -LiteralPath $path }
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    return $path
}

# Ждём ответа /api/status (узел стартует не мгновенно).
function Wait-TestNode {
    param([Parameter(Mandatory)][string]$Base, [int]$Seconds = 30)
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        try { return Invoke-RestMethod "$Base/api/status" -TimeoutSec 2 }
        catch { Start-Sleep -Milliseconds 300 }
    }
    throw "узел $Base не отвечает за $Seconds с"
}

# Запуск узла. Возвращает объект: Process, Base, HttpPort, Name, Config, Log.
function Start-TestNode {
    param(
        [Parameter(Mandatory)][int]$HttpPort,
        [Parameter(Mandatory)][int]$UdpPort,
        [Parameter(Mandatory)][string]$Name,
        [string[]]$Shares = @(),      # строки "имя=путь"
        [int]$FtpPort = 0,            # 0 — FTP выключен
        [int]$Interval = 3,
        [string]$Config,              # по умолчанию build\tmp\<имя>-<порт>.ini
        [switch]$NoSync
    )
    $exe = Get-ObmenExe
    if (-not $Config) {
        $dir = Join-Path $RepoRoot 'build\tmp'
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        $Config = Join-Path $dir ("{0}-{1}.ini" -f $Name, $HttpPort)
        Remove-Item -LiteralPath $Config -Force -ErrorAction SilentlyContinue
    }
    $log = [System.IO.Path]::ChangeExtension($Config, '.log')
    Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue

    $argList = @('--no-ui', '--config', $Config, '--name', $Name,
                 '--port', "$HttpPort", '--udp-port', "$UdpPort", '--interval', "$Interval")
    if ($FtpPort -gt 0) { $argList += @('--ftp-port', "$FtpPort") } else { $argList += '--no-ftp' }
    if ($NoSync) { $argList += '--no-sync' }
    foreach ($s in $Shares) { $argList += @('--share', $s) }

    $p = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $log -ArgumentList $argList
    $node = [pscustomobject]@{
        Process  = $p
        Base     = "http://127.0.0.1:$HttpPort"
        HttpPort = $HttpPort
        Name     = $Name
        Config   = $Config
        Log      = $log
    }
    $null = Wait-TestNode -Base $node.Base
    return $node
}

# Стандартная пара узлов: одинаковый ключ папки на обоих, разные каталоги.
# Файл, положенный на диск одной стороны, должен приехать на другую.
function Start-TestPair {
    param(
        [int]$HttpA = 8091, [int]$HttpB = 8092,
        [int]$FtpA = 2122, [int]$FtpB = 2123,
        [int]$Udp = 45554, [int]$Interval = 3,
        [string]$ShareName = 'share',
        [string]$DirA = 'share1', [string]$DirB = 'share2'
    )
    $pathA = New-TestShare $DirA
    $pathB = New-TestShare $DirB
    $a = Start-TestNode -HttpPort $HttpA -FtpPort $FtpA -UdpPort $Udp -Name 'Node-A' `
        -Interval $Interval -Shares @("$ShareName=$pathA")
    try {
        $b = Start-TestNode -HttpPort $HttpB -FtpPort $FtpB -UdpPort $Udp -Name 'Node-B' `
            -Interval $Interval -Shares @("$ShareName=$pathB")
    } catch {
        Stop-TestNode $a
        throw
    }
    return [pscustomobject]@{
        A = $a; B = $b; ShareA = $pathA; ShareB = $pathB; ShareName = $ShareName
    }
}

# Останавливает только переданные узлы.
function Stop-TestNode {
    param($Node)
    if ($null -eq $Node) { return }
    foreach ($n in @($Node)) {
        try {
            if ($n.Process -and -not $n.Process.HasExited) { Stop-Process -Id $n.Process.Id -Force }
        } catch { }
    }
}

# Убирает «хвосты» прошлых прогонов: только процессы этой же сборки.
function Stop-AllTestNodes {
    Get-Process -Name obmen -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $ExePath } |
        Stop-Process -Force
    Start-Sleep -Milliseconds 500
}

# Edge нужен сценариям с проверкой веб-интерфейса (headless --dump-dom).
function Get-EdgePath {
    $candidates = @()
    if (${env:ProgramFiles(x86)}) { $candidates += Join-Path ${env:ProgramFiles(x86)} 'Microsoft\Edge\Application\msedge.exe' }
    if ($env:ProgramFiles) { $candidates += Join-Path $env:ProgramFiles 'Microsoft\Edge\Application\msedge.exe' }
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) { return $c }
    }
    $cmd = Get-Command msedge.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# Проверка, которая валит прогон (а не просто печатает «False»).
function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "ПРОВЕРКА НЕ ПРОЙДЕНА: $Message" }
}

# Ожидание появления/исчезновения файла.
function Wait-TestPath {
    param([Parameter(Mandatory)][string]$Path, [bool]$Exists = $true, [int]$Seconds = 30)
    for ($i = 0; $i -lt $Seconds * 2; $i++) {
        if ((Test-Path -LiteralPath $Path) -eq $Exists) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}
