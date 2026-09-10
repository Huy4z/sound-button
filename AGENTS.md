# AGENTS.md — sound-button

Windows 桌面悬浮音效按钮：点击即播音效。C++17 / Qt 6（MSYS2 UCRT64, 6.10.x）/ CMake + Ninja。非 git 仓库。UI 与文档使用中文。

## 目录

- `src/` — 全部源码（7 个文件，见下）；`build/` — 开发构建产物（含 `sounds/` 测试音效）；`dist/` — windeployqt 独立打包版（可整体拷走运行）；`sounds/` — 仓库根的音效目录（仅存放用，程序读取的是 **exe 同目录**的 `sounds/`）
- 模块与依赖方向：`SoundButtonWidget`（UI）→ `SoundLibrary`（列表/预解码/config.json）→ `AudioEngine`（QAudioSink 播放）。UI 不直接碰音频。

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

- **低延迟是核心需求**（点击到出声 10~30ms）：音效在添加/启动时由 `QAudioDecoder` 后台整体解码成内存 PCM（`SoundEntry::pcm`），点击时只做 QAudioSink 启动+推流。禁止在点击路径上引入解码/媒体管线（如 QMediaPlayer）。播放触发在鼠标**按下**而非松开。
- `AudioEngine` 复用同一 QAudioSink（格式相同不重建）；`m_data` 必须与 QBuffer 同生命周期。
- 配置便携式：`config.json` 写在 `QCoreApplication::applicationDirPath()` 下；首次运行自动导入 exe 旁 `sounds/`。音效列表/当前项/音量/窗口位置/置顶都存这里。
- 按下即播 + 拖动判定的交互契约：移动超过 8px 判定为拖动并停掉误播（见 SoundButtonWidget::mouseMoveEvent），改交互时保持此行为。

## Qt 6.10 API 坑（已踩过）

- `QAudioDecoder`：信号名是 `error(QAudioDecoder::Error)`，且与取值函数 `error()` 同名，connect 需 `static_cast<void (QAudioDecoder::*)(QAudioDecoder::Error)>`；没有 `errorOccurred`。
- `QAudioBuffer`：非模板 `constData()`/`data()` 是 **private**，取原始字节用模板 `constData<char>()`。
- `AudioEngine.h`/头文件中的 `std::unique_ptr<QAudioSink/QBuffer>` 成员：头文件直接 include 对应头（moc 会实例化完整类型）。

## Windows / 平台注意

- CMake 用 `qt_add_executable(... WIN32 ...)`，无控制台；`qWarning()` 双击运行时不可见，调试时从带 stderr 重定向的终端启动。
- 窗口 flags `FramelessWindowHint | Tool | WindowStaysOnTopHint` + `WA_TranslucentBackground`：不进任务栏，也不会出现在 UIA/list_apps 应用列表（桌面自动化验证用截屏，别用 get_app_state 按 pid 找它）。切换置顶 flag 后窗口会隐藏，必须再 `show()`。
- Qt `move()` 用逻辑坐标，物理像素 = 逻辑 × DPI 缩放（本机 125%）；鼠标事件 `globalPosition()` 是逻辑坐标。
- 中文文件名/路径经 `QUrl::fromLocalFile` 处理正常，config.json 为 UTF-8（控制台 cat 显示乱码是代码页问题，文件本身正确）。

## 测试小技巧

用 MSYS2 ffmpeg 造测试音：`ffmpeg -f lavfi -i "sine=frequency=880:duration=1" -codec:a libmp3lame -q:a 4 build/sounds/x.mp3`。验证播放只能确认进程存活+解码日志，**声音听感需真人确认**。

详细功能与用法见 `README.md`。
