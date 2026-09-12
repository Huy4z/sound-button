# 生成 docs/architecture.png —— 项目架构图（无第三方依赖，System.Drawing 手绘）
# 用法：pwsh -File tools/gen-architecture-diagram.ps1
Add-Type -AssemblyName System.Drawing

$W = 1520; $H = 1040
$bmp = New-Object System.Drawing.Bitmap($W, $H)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
$g.Clear([System.Drawing.Color]::FromArgb(246, 248, 251))

$fTitle = New-Object System.Drawing.Font("Microsoft YaHei", 21, [System.Drawing.FontStyle]::Bold)
$fBox   = New-Object System.Drawing.Font("Microsoft YaHei", 13.5, [System.Drawing.FontStyle]::Bold)
$fBody  = New-Object System.Drawing.Font("Microsoft YaHei", 11)
$fSmall = New-Object System.Drawing.Font("Microsoft YaHei", 10)
$fLabel = New-Object System.Drawing.Font("Microsoft YaHei", 10)

$cText  = [System.Drawing.Color]::FromArgb(32, 36, 44)
$cGray  = [System.Drawing.Color]::FromArgb(95, 99, 104)
$cBlue  = [System.Drawing.Color]::FromArgb(43, 107, 242)
$cGreen = [System.Drawing.Color]::FromArgb(30, 142, 62)
$cAmber = [System.Drawing.Color]::FromArgb(230, 145, 0)
$cRed   = [System.Drawing.Color]::FromArgb(217, 48, 37)
$bg     = [System.Drawing.Color]::FromArgb(246, 248, 251)

function New-RoundRect([double]$x, [double]$y, [double]$w, [double]$h, [double]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

function Draw-Box([double]$x, [double]$y, [double]$w, [double]$h, $fill, $border, [string]$title, [string[]]$lines) {
    $path = New-RoundRect $x $y $w $h 12
    $g.FillPath((New-Object System.Drawing.SolidBrush($fill)), $path)
    $g.DrawPath((New-Object System.Drawing.Pen($border, 2)), $path)
    $g.DrawString($title, $fBox, (New-Object System.Drawing.SolidBrush($border)), [float]($x + 22), [float]($y + 16))
    $ly = $y + 50
    foreach ($line in $lines) {
        $g.DrawString($line, $fBody, (New-Object System.Drawing.SolidBrush($cText)), [float]($x + 24), [float]$ly)
        $ly += 27
    }
}

function Draw-Arrow([double]$x1, [double]$y1, [double]$x2, [double]$y2, $color, [bool]$dashed = $false, [double]$thick = 2.2) {
    $pen = New-Object System.Drawing.Pen($color, $thick)
    if ($dashed) { $pen.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash }
    $g.DrawLine($pen, [float]$x1, [float]$y1, [float]$x2, [float]$y2)
    $ang = [Math]::Atan2($y2 - $y1, $x2 - $x1)
    $a = 24 * [Math]::PI / 180
    $p1 = New-Object System.Drawing.PointF([float]($x2 - 17 * [Math]::Cos($ang - $a)), [float]($y2 - 17 * [Math]::Sin($ang - $a)))
    $p2 = New-Object System.Drawing.PointF([float]($x2 - 17 * [Math]::Cos($ang + $a)), [float]($y2 - 17 * [Math]::Sin($ang + $a)))
    $tip = New-Object System.Drawing.PointF([float]$x2, [float]$y2)
    $g.FillPolygon((New-Object System.Drawing.SolidBrush($color)), [System.Drawing.PointF[]]@($tip, $p1, $p2))
}

function Draw-Label([string]$text, [double]$cx, [double]$cy, $color) {
    $size = $g.MeasureString($text, $fLabel)
    $x = $cx - $size.Width / 2; $y = $cy - $size.Height / 2
    $g.FillRectangle((New-Object System.Drawing.SolidBrush($bg)), [float]($x - 5), [float]($y - 1), [float]($size.Width + 10), [float]($size.Height + 2))
    $g.DrawString($text, $fLabel, (New-Object System.Drawing.SolidBrush($color)), [float]$x, [float]$y)
}

# ---------- 标题 ----------
$g.DrawString("sound-button 项目架构", $fTitle, (New-Object System.Drawing.SolidBrush($cText)), 40, 26)
$g.DrawString("Windows 桌面悬浮音效按钮 · C++17 / Qt 6 · 点击即播（按下 → 推流 0.1~0.9ms）", $fSmall,
              (New-Object System.Drawing.SolidBrush($cGray)), 42, 72)

# ---------- 用户操作 ----------
Draw-Box 520 106 480 78 ([System.Drawing.Color]::FromArgb(241, 243, 244)) $cGray "用户操作" @()
$g.DrawString("左键按下（按下即播）· 右键菜单切换音效 · 托盘图标", $fBody,
              (New-Object System.Drawing.SolidBrush($cText)), 542, 150)

# ---------- UI 层 ----------
Draw-Box 140 244 1240 168 ([System.Drawing.Color]::FromArgb(232, 240, 254)) $cBlue "SoundButtonWidget（UI 层 · 主线程）" @(
    "界面只有一个 QQ emoji 按钮（10 帧按下动画）+ 右上角图钉（切换置顶），其余设置全在右键菜单",
    "mousePress 按下即播 · mouseMove 位移 > 8px 判定拖动并停声 · contextMenu 菜单",
    "启动 300ms 后 prepareFormats()：把设备枚举与建流成本移出点击路径；entryReady / 输出设备变化时随手预热"
)

# ---------- 数据层 / 播放层 ----------
Draw-Box 140 468 590 250 ([System.Drawing.Color]::FromArgb(230, 244, 234)) $cGreen "SoundLibrary（数据层）" @(
    "SoundEntry[]：path / name / format / pcm / ready",
    "QAudioDecoder 后台整体解码成内存 PCM",
    "音频数据原样保留（不裁剪 / 不改写）",
    "config.json（QSaveFile 原子写）：列表 / 当前项 /",
    "音量 / 窗口位置 / 置顶"
)
Draw-Box 790 468 590 250 ([System.Drawing.Color]::FromArgb(254, 247, 224)) $cAmber "AudioEngine（播放层）" @(
    "Stream[] 热流池：按采样格式各一条，LRU 最多 8 条",
    "QAudioSink（已初始化）+ QBuffer（引用 PCM，不复制）",
    "prepare()/warmUp()：5ms 静音预热，流停在 Idle",
    "play()：空闲态 start；播放中 suspend → resume",
    "stop()：只 suspend，保住热流（拖动取消误播用）"
)

# ---------- 素材 ----------
Draw-Box 1080 736 300 96 ([System.Drawing.Color]::FromArgb(240, 240, 245)) ([System.Drawing.Color]::FromArgb(120, 120, 140)) "assets/（编进 exe）" @(
    "button_0..9.png：按下动画帧",
    "pin / pin_fill.png：图钉"
)

# ---------- 平台层 / 诊断 ----------
Draw-Box 430 790 660 96 ([System.Drawing.Color]::FromArgb(241, 243, 244)) $cGray "Qt Multimedia → WASAPI → 输出设备" @(
    "蓝牙耳机 / 板载声卡 / HDMI（设备自身缓冲 十几~上百 ms）"
)
Draw-Box 430 914 660 74 ([System.Drawing.Color]::FromArgb(252, 232, 230)) $cRed "LatencyLog.h（诊断打点）" @(
    "SOUNDBUTTON_LATENCY_LOG=1：每次点击打印「按下 → 推流」"
)

# ---------- 箭头 ----------
Draw-Arrow 760 184 760 244 $cGray
Draw-Label "鼠标事件" 806 212 $cGray

Draw-Arrow 340 412 340 468 $cGreen
Draw-Label "列表 / 当前项 / 音量" 245 436 $cGreen
Draw-Arrow 470 468 470 414 $cBlue $true 1.8
Draw-Label "entryReady 信号" 600 440 $cBlue
Draw-Arrow 1120 412 1140 468 $cAmber
Draw-Label "play(pcm, format)" 1245 436 $cAmber

Draw-Arrow 730 545 790 545 $cGreen $false 2.6
Draw-Label "共享 PCM（零拷贝）" 762 444 $cGreen

Draw-Arrow 760 718 760 790 $cAmber
Draw-Label "start / suspend→resume" 890 754 $cAmber

$out = Join-Path (Split-Path -Parent $PSScriptRoot) "docs\architecture.png"
New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output ("saved: " + $out)
