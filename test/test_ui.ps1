# Проверка встроенного окна (WebView2) и иконки в трее.
. (Join-Path $PSScriptRoot 'common.ps1')
$root = Split-Path -Parent $PSScriptRoot
$exe = Get-ObmenExe
$t = Join-Path $root 'test'
$share = Join-Path $t 'shareA'
$log = Join-Path $t 'ui.log'
$cfg = Join-Path $t 'ui.ini'

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class U {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

function Class-Of([IntPtr]$h) {
    $sb = New-Object System.Text.StringBuilder 256
    [void][U]::GetClassNameW($h, $sb, 256)
    return $sb.ToString()
}
function Text-Of([IntPtr]$h) {
    $sb = New-Object System.Text.StringBuilder 256
    [void][U]::GetWindowTextW($h, $sb, 256)
    return $sb.ToString()
}
function Find-Main {
    $script:found = [IntPtr]::Zero
    $cb = [U+EnumProc]{
        param($h, $l)
        if ((Class-Of $h) -eq 'ObmenMainWindow') { $script:found = $h; return $false }
        return $true
    }
    [void][U]::EnumWindows($cb, [IntPtr]::Zero)
    return [IntPtr]$script:found
}

Stop-AllTestNodes
Remove-Item -Force $log, $cfg -ErrorAction SilentlyContinue

$env:WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS = '--remote-debugging-port=9226'
Write-Host '--- запуск окна ---'
$p = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden -RedirectStandardOutput $log -ArgumentList @(
    '--share', "Общая=$share", '--name', 'Node-UI',
    '--port', '8093', '--ftp-port', '2124', '--udp-port', '45555',
    '--config', $cfg, '--interval', '5')

try {
    $deadline = (Get-Date).AddSeconds(30)
    $hwnd = [IntPtr]::Zero
    while ((Get-Date) -lt $deadline) {
        $hwnd = Find-Main
        if ($hwnd -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 400
    }
    if ($hwnd -eq [IntPtr]::Zero) { throw 'окно ObmenMainWindow не найдено' }
    Start-Sleep -Seconds 3

    Write-Host ("  окно: h=" + $hwnd + " класс=" + (Class-Of $hwnd) + " заголовок='" + (Text-Of $hwnd) + "'")
    $r = New-Object U+RECT
    [void][U]::GetWindowRect($hwnd, [ref]$r)
    Write-Host ("  размер: " + ($r.Right - $r.Left) + "x" + ($r.Bottom - $r.Top))
    if (-not [U]::IsWindowVisible($hwnd)) { throw 'окно не показано' }

    $chrome = New-Object System.Collections.ArrayList
    $cb = [U+EnumProc]{
        param($h, $l)
        $c = Class-Of $h
        if ($c -like 'Chrome_*') { [void]$script:chrome.Add($c) }
        return $true
    }
    [void][U]::EnumChildWindows($hwnd, $cb, [IntPtr]::Zero)
    Write-Host ("  дочерние окна WebView2: " + $chrome.Count + " " + (($chrome | Select-Object -Unique) -join ', '))
    if ($chrome.Count -eq 0) { throw 'не найден встроенный просмотр (WebView2)' }

    Write-Host '--- содержимое встроенного просмотра (remote debugging) ---'
    $pageOk = $false
    for ($i = 0; $i -lt 15; $i++) {
        try {
            $targets = Invoke-RestMethod 'http://127.0.0.1:9226/json' -TimeoutSec 2
            $page = $targets | Where-Object { $_.type -eq 'page' } | Select-Object -First 1
            if ($page) {
                Write-Host ("  страница: " + $page.url)
                Write-Host ("  заголовок: " + $page.title)
                if ($page.url -like 'http://127.0.0.1:8093/*') { $pageOk = $true }
                break
            }
        } catch { }
        Start-Sleep -Milliseconds 600
    }
    if ($pageOk) { Write-Host '  в окне открыт наш интерфейс: OK' }
    else { Write-Host '  (отладчик WebView2 недоступен — проверка по дочерним окнам)' }

    Write-Host '--- журнал приложения ---'
    Start-Sleep -Milliseconds 400
    Get-Content $log -ErrorAction SilentlyContinue | Select-Object -Last 12 | ForEach-Object { Write-Host ("  " + $_) }
    $logText = Get-Content $log -Raw -ErrorAction SilentlyContinue
    if ($logText -notmatch 'окно программы создано') { throw 'в журнале нет строки о создании окна' }
    if ($logText -notmatch 'иконка добавлена в системный трей') { throw 'иконка в трей не добавлена' }
    if ($logText -notmatch 'встроенный интерфейс открыт') { throw 'встроенный интерфейс не открылся' }

    Write-Host '--- сворачивание в трей по крестику ---'
    [void][U]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)  # WM_CLOSE
    Start-Sleep -Milliseconds 900
    Write-Host ("  окно видно: " + [U]::IsWindowVisible($hwnd))
    Write-Host ("  процесс жив: " + (-not $p.HasExited))
    if ([U]::IsWindowVisible($hwnd)) { throw 'окно не свернулось в трей' }
    if ($p.HasExited) { throw 'процесс завершился вместо сворачивания' }

    Write-Host '--- возврат окна из трея ---'
    [void][U]::PostMessage($hwnd, 0x0111, [IntPtr]1300, [IntPtr]::Zero)  # WM_COMMAND ID_TRAY_SHOW
    Start-Sleep -Milliseconds 900
    Write-Host ("  окно видно: " + [U]::IsWindowVisible($hwnd))
    if (-not [U]::IsWindowVisible($hwnd)) { throw 'окно не вернулось из трея' }

    Write-Host '--- выход через меню ---'
    [void][U]::PostMessage($hwnd, 0x0111, [IntPtr]1202, [IntPtr]::Zero)  # WM_COMMAND ID_MENU_EXIT
    $p.WaitForExit(10000) | Out-Null
    Write-Host ("  процесс завершён: " + $p.HasExited + " код: " + $(if ($p.HasExited) { $p.ExitCode } else { 'нет' }))
    if (-not $p.HasExited) { throw 'процесс не завершился по команде выхода' }

    Write-Host 'ИТОГ: окно, трей и веб-интерфейс внутри exe работают'
}
finally {
    Stop-AllTestNodes
    Remove-Item Env:\WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS -ErrorAction SilentlyContinue
}
