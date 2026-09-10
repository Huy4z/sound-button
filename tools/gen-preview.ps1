# 生成 preview.png（README 顶部的界面预览图）
# 用法：pwsh -File tools/gen-preview.ps1
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root "preview.png"

$W = 980; $H = 400
$bmp = New-Object System.Drawing.Bitmap($W, $H)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
$g.Clear([System.Drawing.Color]::FromArgb(243, 245, 248))

$fCap = New-Object System.Drawing.Font("Microsoft YaHei", 13)
$cCap = [System.Drawing.Color]::FromArgb(90, 96, 105)

# 把黑色剪影染成指定颜色（用 ColorMatrix 把 RGB 换成目标色、保留 alpha）
function Tint([System.Drawing.Image]$img, $color) {
    $pm = New-Object System.Drawing.Bitmap($img.Width, $img.Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $pg = [System.Drawing.Graphics]::FromImage($pm)
    $pg.Clear([System.Drawing.Color]::Transparent)
    $mat = New-Object System.Drawing.Imaging.ColorMatrix
    $mat.Matrix00 = 0; $mat.Matrix11 = 0; $mat.Matrix22 = 0; $mat.Matrix33 = 1
    $mat.Matrix40 = $color.R / 255.0
    $mat.Matrix41 = $color.G / 255.0
    $mat.Matrix42 = $color.B / 255.0
    $ia = New-Object System.Drawing.Imaging.ImageAttributes
    $ia.SetColorMatrix($mat)
    $dstRect = New-Object System.Drawing.Rectangle(0, 0, $img.Width, $img.Height)
    $pg.DrawImage($img, $dstRect, 0, 0, $img.Width, $img.Height, [System.Drawing.GraphicsUnit]::Pixel, $ia)
    $pg.Dispose()
    return $pm
}

function Draw-Tile([int]$x, [int]$y, [string]$framePng, [bool]$pinned, [string]$caption) {
    $size = 190
    $btn = [System.Drawing.Image]::FromFile((Join-Path $root "assets\$framePng"))
    $g.DrawImage($btn, $x, $y, $size, $size)
    $btn.Dispose()

    # 右上角图钉徽标（和程序里同一套画法：白圆底 + 染色图标）
    $d = 46
    $bx = $x + $size - $d + 6
    $by = $y - 6
    $alpha = if ($pinned) { 240 } else { 170 }
    $brush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb($alpha, 255, 255, 255))
    $g.FillEllipse($brush, $bx, $by, $d, $d)
    $brush.Dispose()
    if ($pinned) {
        $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(110, 43, 107, 242), 2)
        $g.DrawEllipse($pen, ($bx + 1), ($by + 1), ($d - 2), ($d - 2))
        $pen.Dispose()
    }
    $pinFile = if ($pinned) { "pin_fill.png" } else { "pin.png" }
    $pinColor = if ($pinned) { [System.Drawing.Color]::FromArgb(43, 107, 242) } else { [System.Drawing.Color]::FromArgb(95, 99, 104) }
    $srcPin = [System.Drawing.Image]::FromFile((Join-Path $root "assets\$pinFile"))
    $pin = Tint $srcPin $pinColor
    $srcPin.Dispose()
    $icon = 26
    $g.DrawImage($pin, ($bx + ($d - $icon) / 2), ($by + ($d - $icon) / 2), $icon, $icon)
    $pin.Dispose()

    $fmt = New-Object System.Drawing.StringFormat
    $fmt.Alignment = [System.Drawing.StringAlignment]::Center
    $capX = $x - 30
    $capY = $y + $size + 16
    $capW = $size + 60
    $rect = New-Object System.Drawing.RectangleF($capX, $capY, $capW, 40)
    $g.DrawString($caption, $fCap, (New-Object System.Drawing.SolidBrush($cCap)), $rect, $fmt)
}

Draw-Tile 70 70 "button_0.png" $false "待机 · 未置顶"
Draw-Tile 395 70 "button_0.png" $true "待机 · 已置顶"
Draw-Tile 720 70 "button_6.png" $true "按下"

$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output ("saved: " + $out)