# 把 Lottie 动画烘焙成 assets/button_*.png（按钮按下动画帧）
#
# 用法：pwsh -File tools/bake-button-frames.ps1 [-Source <button_anime.json>]
# 依赖：Node 不是必需的；只用 Chrome/Edge 无头模式 + lottie-web（缺了会自动下载到 build/lottie/）
#
# 之所以离线烘焙：动画是 16 个矢量图层 + 遮罩 + 渐变，Qt 没有 Lottie 渲染器，
# 运行时如实播放不现实；烘焙成 10 张位图后，程序里只是一次 QString→QPixmap 的贴图。
param(
    [string]$Source = "D:\Projects\icon-source\QQ emojis\续标识\button_anime.json",
    [int]$Frames = 10,
    [int]$Size = 256
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root "build\lottie"
$framesDir = Join-Path $work "frames"
$assets = Join-Path $root "assets"
New-Item -ItemType Directory -Force -Path $work, $framesDir, $assets | Out-Null

# --- 1. 准备 lottie-web（首次运行会联网下载一次，之后走缓存） ---
$lottieJs = Join-Path $work "lottie.min.js"
if (-not (Test-Path $lottieJs)) {
    Write-Output "下载 lottie-web…"
    Invoke-WebRequest -Uri "https://cdn.jsdelivr.net/npm/lottie-web@5.12.2/build/player/lottie.min.js" -OutFile $lottieJs
}

# --- 2. 生成渲染页（动画 JSON 直接内联，避开 file:// 的 XHR 限制） ---
$json = Get-Content $Source -Raw -Encoding UTF8
[System.IO.File]::WriteAllText((Join-Path $work "anim-data.js"), "window.ANIM = $json;", (New-Object System.Text.UTF8Encoding($false)))
$html = @"
<!DOCTYPE html>
<html><head><meta charset="utf-8">
<style>html,body{margin:0;padding:0;background:transparent;overflow:hidden}#box{width:512px;height:512px}</style>
</head><body>
<div id="box"></div>
<script src="lottie.min.js"></script>
<script src="anim-data.js"></script>
<script>
  const p = new URLSearchParams(location.search);
  const frame = parseFloat(p.get('frame') || '0');
  const anim = lottie.loadAnimation({
    container: document.getElementById('box'),
    renderer: 'svg', loop: false, autoplay: false, animationData: window.ANIM
  });
  // 注意：DOMLoaded 回调里的 this 不是动画实例，必须用 anim 变量引用
  anim.addEventListener('DOMLoaded', () => { anim.goToAndStop(frame, true); });
</script>
</body></html>
"@
[System.IO.File]::WriteAllText((Join-Path $work "render.html"), $html, (New-Object System.Text.UTF8Encoding($false)))

# --- 3. 无头浏览器逐帧截图（透明背景，512×512） ---
$browser = @(
    "C:\Program Files\Google\Chrome\Application\chrome.exe",
    "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $browser) { throw "找不到 Chrome 或 Edge" }
$page = "file:///" + ($work -replace '\\', '/') + "/render.html"
foreach ($f in 0..($Frames - 1)) {
    & $browser --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 `
        --default-background-color=00000000 --window-size=512,512 --virtual-time-budget=4000 `
        --screenshot="$framesDir\f$f.png" "$page`?frame=$f" 2>$null | Out-Null
}

# --- 4. 按所有帧的非透明并集裁成正方形，再缩放到目标尺寸 ---
$minX = [int]::MaxValue; $minY = [int]::MaxValue; $maxX = -1; $maxY = -1
foreach ($f in 0..($Frames - 1)) {
    $bmp = [System.Drawing.Bitmap]::FromFile("$framesDir\f$f.png")
    $rect = New-Object System.Drawing.Rectangle(0, 0, $bmp.Width, $bmp.Height)
    $d = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($d.Stride * $bmp.Height)
    [System.Runtime.InteropServices.Marshal]::Copy($d.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($d)
    for ($y = 0; $y -lt $bmp.Height; $y++) {
        $row = $y * $d.Stride
        for ($x = 0; $x -lt $bmp.Width; $x++) {
            if ($bytes[$row + $x * 4 + 3] -gt 8) {
                if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    $bmp.Dispose()
}
$side = [int]([Math]::Max($maxX - $minX + 1, $maxY - $minY + 1) * 1.02)
$sx = [int][Math]::Round(($minX + $maxX) / 2.0 - $side / 2.0)
$sy = [int][Math]::Round(($minY + $maxY) / 2.0 - $side / 2.0)
foreach ($i in 0..($Frames - 1)) {
    $src = [System.Drawing.Bitmap]::FromFile("$framesDir\f$i.png")
    $dst = New-Object System.Drawing.Bitmap($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($dst)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($src,
        (New-Object System.Drawing.Rectangle(0, 0, $Size, $Size)),
        (New-Object System.Drawing.Rectangle($sx, $sy, $side, $side)),
        [System.Drawing.GraphicsUnit]::Pixel)
    $dst.Save("$assets\button_$i.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $dst.Dispose(); $src.Dispose()
}
Write-Output "已生成 $Frames 帧（${Size}x${Size}）到 $assets"