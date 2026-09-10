# AGENTS.md — sound-button

Windows 桌面悬浮音效按钮：点击即播音效。C++17 / Qt 6（MSYS2 UCRT64, 6.10.x）/ CMake + Ninja。git 仓库（`build/`、`dist/`、`.zcode/` 已在 .gitignore）。UI 与文档使用中文。

## 目录

- `src/` — 全部源码（8 个文件，见下）；`build/` — 开发构建产物（含 `sounds/` 测试音效）；`dist/` — windeployqt 独立打包版（可整体拷走运行）；`sounds/` — 仓库根的音效目录（仅存放用，程序读取的是 **exe 同目录**的 `sounds/`）
- `assets/` — 界面素材（按钮 10 帧动画 + 图钉），经 `qt_add_resources` 编进 exe，运行时不需要外部文件；`tools/` — 素材与文档的生成脚本（烘焙动画、预览图、架构图）
- `ARCHITECTURE.md` — 架构说明（分层图、启动/点击两条时序、延迟预算、不变量）；配图 `docs/architecture.png` 由 `tools/gen-architecture-diagram.ps1` 生成
- 模块与依赖方向：`SoundButtonWidget`（UI）→ `SoundLibrary`（列表/预解码/config.json）与 `AudioEngine`（QAudioSink 播放）。后两者互不相识，PCM 由 UI 层以 `shared_ptr` 从数据层交到播放层（零拷贝）。UI 不直接碰音频 API。

## 构建 / 运行 / 打包

必须用 MSYS2 UCRT64 工具链（Git Bash 里加 PATH 前缀，不要用系统其他 cmake/gcc）：

```bash
PATH="/d/Library/msys64/ucrt64/bin:$PATH" cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
PATH="/d/Library/msys64/ucrt64/bin:$PATH" cmake --build build
PATH="/d/Library/msys64/ucrt64/bin:$PATH" ./build/soundbutton.exe   # 运行需同 PATH（加载 Qt DLL）
```

依赖：`pacman -S mingw-w64-ucrt-x86_64-qt6-multimedia`（已装；FFmpeg 后端，负责解码）。

打包 dist（更新代码后需重做）：copy exe → `windeployqt --release --no-compiler-runtime dist/soundbutton.exe` → 递归补齐非 Qt DLL（objdump -p 扫 imports，从 ucrt64/bin 拷 avcodec/avformat/libstdc++/libgcc 等约 100 个）。

## 架构不变量（改代码前必读）

- **低延迟是核心需求**：点击路径上只能做"换数据 + 起播"。音效在添加/启动时由 `QAudioDecoder` 后台整体解码成内存 PCM（`SoundEntry::pcm`，`shared_ptr` 与播放端共享，点击路径零拷贝），并裁掉首尾 -80dBFS 以下的数字静音（很多音效文件自带 80~170ms 开头静音，不裁就是白等）。禁止在点击路径上引入解码/媒体管线（如 QMediaPlayer）。播放触发在鼠标**按下**而非松开。
- **播放路径（实测数据支撑，别改回老写法）**：
  - `AudioEngine` 按**采样格式**各保留一条已初始化的"热" QAudioSink（上限 8 条，LRU 淘汰）。`prepare()` 预热：启动后 300ms 由 `SoundButtonWidget` 触发，把设备枚举 + 建流 + 流初始化（本机 1~3s）从点击路径挪到启动阶段。
  - 播放中再次点击：`suspend()`（会丢弃已排队音频）→ 换 PCM → `resume()`，实测 0.6~1.5ms。
  - **禁止用 `stop()` + `start()` 做重播**：`stop()` 会销毁 WASAPI 流，下次 `start()` 要重建，实测板载 89ms / 蓝牙 50ms / HDMI 13ms。拖动取消误播也用 `stop()`（实现里已是 suspend）。
  - 空闲态（播完）再点击走 `start()`，实测 0.1ms —— 所以要让流停留在 Idle，而不是 Stopped。
  - 输出设备变化（`QMediaDevices::audioOutputsChanged`）时 `invalidateSinks()` 丢弃重建。
  - PCM 缓冲区（`QByteArray`）必须与 QBuffer 同生命周期；现由 `Stream::pcm` + 条目共同持有。
- 配置便携式：`config.json` 写在 `QCoreApplication::applicationDirPath()` 下；首次运行自动导入 exe 旁 `sounds/`。音效列表/当前项/音量/窗口位置/置顶都存这里。
- **交互契约（`SoundButtonWidget`，改界面时保持）**：
  - 界面只有两样东西：中间的 emoji 按钮 + 右上角的图钉。窗口里不放任何文字——音效名走 tooltip 和切换时的 `flashName()` 气泡，状态用浓淡表达（解码中 55% 透明度，解码失败 40% + 右下角红点）。
  - 左键**按下**即播，不等松开；移动超过 8px 判定为拖动并停掉误播（见 `SoundButtonWidget::mouseMoveEvent`）。
  - 图钉（`pinRect()` 命中区）只切换置顶，不播音效、不触发按下动画；它是置顶的主入口，右键菜单里那项与之共用 `m_lib->alwaysOnTop()`，两边必须同步。
  - 窗口尺寸 = `kButtonSize + 2*kWindowPad`，图钉徽标压在按钮右上角；改大小只需改这两个常量（帧缓存按设备像素比自动重建）。

## 界面素材

- 按钮动画取自 QQ emoji「续标识」（`D:\Projects\icon-source\QQ emojis\续标识\`）的 Lottie 文件，**离线烘焙**成 `assets/button_0..9.png`（10 帧 256×256，0=松开 9=完全按下）。
  重新烘焙：`pwsh -File tools/bake-button-frames.ps1`（首次联网下载 lottie-web 到 `build/lottie/`，再用 Chrome/Edge 无头模式逐帧截图 → 裁并集包围盒 → 缩放到 256）。
  为什么不在运行时渲染 Lottie：那份动画是 16 个矢量图层 + 遮罩 + 渐变 + 轨道遮罩，Qt 没有现成渲染器；烘焙成位图后，程序里只是 10 张图按帧贴出来。
  烘焙页里的坑：`DOMLoaded` 回调中的 `this` 不是动画实例，必须用 `anim` 变量引用，否则 `goToAndStop` 静默失效（表现为所有帧长得一样）。帧号通过 `?frame=N` 传入。
- 图钉用 Ant Design 官方图标库的 `pushpin` / `pushpin-fill`（`D:\Projects\icon-source\Ant Design 官方图标库\`），复制为 `assets/pin.png` / `assets/pin_fill.png`，运行时 `tinted()` 染成灰色/主题蓝。
- 换素材后顺手重跑：`tools/gen-preview.ps1`（`preview.png`）与 `tools/gen-architecture-diagram.ps1`（`docs/architecture.png`）。

## 文档与注释

- 代码注释一律中文，讲**为什么**（实测数字、踩过的坑、不能改回老写法的原因），不复述代码字面在做什么；改动逻辑时顺手更新同处注释。
- 动到分层、依赖方向或延迟路径时，同步更新 `ARCHITECTURE.md`，并重跑 `pwsh -File tools/gen-architecture-diagram.ps1` 刷新 `docs/architecture.png`。
- 面向使用者的功能变化写进 `README.md`；本文件只记录"改代码需要知道的事"，不重复 README 的用法说明。

## 延迟怎么验证

从终端启动：`set SOUNDBUTTON_LATENCY_LOG=1 && build\soundbutton.exe 2> lat.log`，每次点击打印「按下 → 推流 x ms」与路径（`start` / `suspend→resume`）。

合成点击（PowerShell + `EnumWindows` 找可见窗口，再 `PostMessage`）：左键 `WM_LBUTTONDOWN(0x0201)/WM_LBUTTONUP(0x0202)`，滚轮 `WM_MOUSEWHEEL(0x020A)`（delta 在高 16 位），右键 `WM_RBUTTONDOWN/UP(0x0204/0x0205)`。三个坑：

1. lParam 是**物理像素**的客户区坐标——本机 125% 缩放，逻辑坐标要 ×1.25，否则点不到图钉这种小目标；
2. 合成的 `WM_MOUSEMOVE(0x0200)` **不能用来测拖动**：Qt 算鼠标坐标时会掺进真实光标的位置，位移可能放大或方向都不对（看着像程序 bug，其实是测试假象）。拖动验证请注入真实光标（`SetCursorPos` + `mouse_event(LEFTDOWN / 分步 MOVE / LEFTUP)`），实测窗口 1:1 跟随、config 同步；
3. 打点日志本身要写 stderr 且别把 `fprintf` 放在点击→推流之间，否则测出来的是日志 I/O 的耗时。

## Qt 6.10 API 坑（已踩过）

- `QAudioDecoder`：信号名是 `error(QAudioDecoder::Error)`，且与取值函数 `error()` 同名，connect 需 `static_cast<void (QAudioDecoder::*)(QAudioDecoder::Error)>`；没有 `errorOccurred`。
- `QAudioBuffer`：非模板 `constData()`/`data()` 是 **private**，取原始字节用模板 `constData<char>()`。
- `AudioEngine.h`/头文件中的 `std::unique_ptr<QAudioSink/QBuffer>` 成员：头文件直接 include 对应头（moc 会实例化完整类型）。
- `QAudioSink::stop()` ↔ `start()` 不是"暂停/继续"，而是销毁/重建流（13~125ms）；暂停用 `suspend()`/`resume()`。`suspend()` 会丢弃已排队的音频，所以重播时先 suspend 再换数据不会听到上一段的尾巴。
- `QMediaDevices::audioOutputs()` / `defaultAudioOutput()` 首次调用要枚举设备（本机 1~3s，走 Media Foundation，会卡住主线程），别放在点击路径或首帧之前。
- GUI 子系统（`WIN32` 可执行）里 Qt 把 `qInfo/qWarning` 送去调试器，即使 stderr 被重定向也收不到；要可靠落盘就自己 `fprintf(stderr, ...)`（见 `LatencyLog.h`），且日志本身要 `fflush`。
- 每个进程收到的**第一条鼠标消息**在系统侧有约 10ms 的额外投递延迟（之后同进程内的点击都是 0.3~1ms），应用内无法消除。
- `QAudioDecoder` 输出浮点 PCM（`QAudioFormat::Float`），解码一条 5 秒 48kHz 立体声要 1 秒多，所以必须后台预解码。

## Windows / 平台注意

- CMake 用 `qt_add_executable(... WIN32 ...)`，无控制台；`qWarning()` 双击运行时不可见，调试时从带 stderr 重定向的终端启动。
- 窗口 flags `FramelessWindowHint | Tool | WindowStaysOnTopHint` + `WA_TranslucentBackground`：不进任务栏，也不会出现在 UIA/list_apps 应用列表（桌面自动化验证用截屏，别用 get_app_state 按 pid 找它）。切换置顶 flag 后窗口会隐藏，必须再 `show()`。
- Qt `move()` 用逻辑坐标，物理像素 = 逻辑 × DPI 缩放（本机 125%）；鼠标事件 `globalPosition()` 是逻辑坐标。
- 中文文件名/路径经 `QUrl::fromLocalFile` 处理正常，config.json 为 UTF-8（控制台 cat 显示乱码是代码页问题，文件本身正确）。

## 测试小技巧

用 MSYS2 ffmpeg 造测试音：`ffmpeg -f lavfi -i "sine=frequency=880:duration=1" -codec:a libmp3lame -q:a 4 build/sounds/x.mp3`。验证播放只能确认进程存活+解码日志，**声音听感需真人确认**。

看界面长什么样别用 `CopyFromScreen` 截屏：窗口没置顶时会被别的窗口挡住，刚切过置顶 flag 还可能抓到旧帧（同一位置连开多个实例更是互相遮挡，config.json 位置一样，看起来就像"图钉状态没变"）。改用 `PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT=2)` 直接抓窗口自己的渲染内容（PowerShell 里 `Graphics.FromImage` + `GetHdc` 即可），按下动画每一帧、图钉两态都能这样逐像素比对（图钉：蓝像素 ≥1 = 已置顶，灰 = 未置顶）。

详细功能与用法见 `README.md`，架构与延迟原理见 `ARCHITECTURE.md`。
