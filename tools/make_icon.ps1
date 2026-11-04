# Генерация res/obmen.ico (градиентный квадрат с двумя стрелками).
# Запуск: pwsh -NoProfile -File tools\make_icon.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$resDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'res'
New-Item -ItemType Directory -Force -Path $resDir | Out-Null

function New-IconBitmap([int]$s) {
    $bmp = [System.Drawing.Bitmap]::new($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    $pad = [Math]::Max(1.0, $s * 0.055)
    $side = $s - 2 * $pad
    $r = [Math]::Max(2.0, $s * 0.235) * 2
    $x = $pad
    $y = $pad
    $p = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $p.AddArc([single]$x, [single]$y, [single]$r, [single]$r, 180, 90)
    $p.AddArc([single]($x + $side - $r), [single]$y, [single]$r, [single]$r, 270, 90)
    $p.AddArc([single]($x + $side - $r), [single]($y + $side - $r), [single]$r, [single]$r, 0, 90)
    $p.AddArc([single]$x, [single]($y + $side - $r), [single]$r, [single]$r, 90, 90)
    $p.CloseFigure()

    $c1 = [System.Drawing.ColorTranslator]::FromHtml('#2FD3B6')
    $c2 = [System.Drawing.ColorTranslator]::FromHtml('#3B7BFF')
    $br = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        [System.Drawing.PointF]::new(0, 0),
        [System.Drawing.PointF]::new([single]$s, [single]$s), $c1, $c2)
    $g.FillPath($br, $p)

    $k = $s / 256.0
    $white = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::White)

    # верхняя стрелка — вправо
    $g.FillRectangle($white, [single](50 * $k), [single](83 * $k), [single](108 * $k), [single](32 * $k))
    [System.Drawing.PointF[]]$pts1 = @(
        [System.Drawing.PointF]::new([single](158 * $k), [single](67 * $k)),
        [System.Drawing.PointF]::new([single](158 * $k), [single](131 * $k)),
        [System.Drawing.PointF]::new([single](202 * $k), [single](99 * $k))
    )
    $g.FillPolygon($white, $pts1)

    # нижняя стрелка — влево
    $g.FillRectangle($white, [single](98 * $k), [single](141 * $k), [single](108 * $k), [single](32 * $k))
    [System.Drawing.PointF[]]$pts2 = @(
        [System.Drawing.PointF]::new([single](98 * $k), [single](125 * $k)),
        [System.Drawing.PointF]::new([single](98 * $k), [single](189 * $k)),
        [System.Drawing.PointF]::new([single](54 * $k), [single](157 * $k))
    )
    $g.FillPolygon($white, $pts2)

    $g.Dispose()
    return $bmp
}

$sizes = @(16, 24, 32, 48, 64, 256)
$frames = @()
foreach ($s in $sizes) {
    $bmp = New-IconBitmap $s
    $ms = [System.IO.MemoryStream]::new()
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $frames += [pscustomobject]@{ Size = $s; Data = $ms.ToArray() }
    $bmp.Dispose()
}

$out = [System.IO.MemoryStream]::new()
$bw = [System.IO.BinaryWriter]::new($out)
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$frames.Count)
$offset = 6 + 16 * $frames.Count
foreach ($f in $frames) {
    $dim = if ($f.Size -ge 256) { 0 } else { $f.Size }
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]$dim)
    $bw.Write([Byte]0)
    $bw.Write([Byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$f.Data.Length)
    $bw.Write([UInt32]$offset)
    $offset += $f.Data.Length
}
foreach ($f in $frames) { $bw.Write($f.Data) }
$bw.Flush()

$icoPath = Join-Path $resDir 'obmen.ico'
[System.IO.File]::WriteAllBytes($icoPath, $out.ToArray())
Write-Host "Записано: $icoPath ($((Get-Item $icoPath).Length) байт, кадры: $($sizes -join ', '))"

# Проверка: цвета в характерных точках кадра 256x256
$probe = New-IconBitmap 256
$arrow = $probe.GetPixel(100, 99)
$bg = $probe.GetPixel(128, 20)
Write-Host "пиксель стрелки (100,99): R=$($arrow.R) G=$($arrow.G) B=$($arrow.B) A=$($arrow.A)"
Write-Host "пиксель фона   (128,20): R=$($bg.R) G=$($bg.G) B=$($bg.B) A=$($bg.A)"
$probe.Dispose()

$ico = [System.Drawing.Icon]::new($icoPath, 32, 32)
Write-Host "System.Drawing читает иконку: $($ico.Width)x$($ico.Height)"
$ico.Dispose()
