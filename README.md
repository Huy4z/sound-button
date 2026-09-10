# 桌面音效按钮（Sound Button）

一个常驻桌面的悬浮小按钮，点击即播音效。C++17 / Qt 6 / CMake 构建，Windows 下免安装、低延迟。

![预览](preview.png)

## 功能

- **界面**：只有一个按钮和一个图钉。按钮是 QQ emoji「续标识」的红按钮（按下会播 10 帧的按压动画，松开瞬时复位，没有回弹动画），右上角图钉用来开关窗口置顶（实心蓝=置顶，空心灰=不置顶）。其余设置全部在右键菜单里。
- **点击即播**：解码在添加时后台完成（顺手裁掉首尾静音），输出设备在启动时预热，按下到推流约 0.1~0.8ms（实测）；再点一次是"从头重播"，同样只要 ~0.6ms。
- **再次点击**：停止并从头重播。
- **右键菜单**：
  - 音效列表（点选切换并播放）
  - 添加音效…（多选文件）
  - 移除当前音效
  - 音量（滑杆）
  - 窗口置顶 开/关（和右上角图钉是同一个开关）
  - 退出
- **拖动**：按住按钮拖动可移动位置，自动保存；位移不超过 8px 仍算点击，不会打断音效。
- **托盘图标**：单击显示/隐藏按钮，右键可退出。
- **格式**：mp3 / wav / ogg / flac / m4a / aac / opus / wma 等（经 FFmpeg 解码），目前以 mp3 为主。
- **记忆**：音效列表、当前音效（重启后自动恢复，配置里存为 `lastPlayed`）、音量、窗口位置、置顶状态存于 exe 旁 `config.json`，便携可带走。

## 延迟（实测，Qt 6.10 / WASAPI）

| 环节 | 优化前 | 优化后 |
| --- | --- | --- |
| 首次点击 | 设备枚举 + 建流 + 流初始化，1~3s 全卡在点击里 | 启动后 300ms 后台预热，点击不再付 |
| 播放中再点（重播） | `stop()+start()` 重建 WASAPI 流：13~125ms（板载声卡 89ms / 蓝牙 50ms / HDMI 13ms） | `suspend()+resume()`：0.6~1.5ms |
| 播完后再点 | 0.1ms | 0.1ms（流保持热） |
| 音效自带开头静音 | 80~170ms 照原样播出 | 解码时裁掉，0ms |
| PCM 拷贝 | 每次点击拷贝整段 | 零拷贝（条目与播放端共享） |

想在自己机器上看数字：从终端启动并加环境变量，每次点击都会打印「按下 → 推流」的耗时与所走路径：

```bat
set SOUNDBUTTON_LATENCY_LOG=1 && build\soundbutton.exe 2> lat.log
```

蓝牙耳机、HDMI 这类输出设备本身还有几十毫秒缓冲延迟，应用侧无法消除；本机默认输出是蓝牙时，实测点击到出声主要就由这段决定。

## 使用

把音效文件放进 exe 旁的 `sounds/` 文件夹后首次启动会自动全部导入；或运行后右键 →「添加音效…」手动选择。

- 开发运行：`build/soundbutton.exe`（需 MSYS2 UCRT64 环境的 PATH）
- 独立运行：`dist/soundbutton.exe`（已打包全部依赖，可单独拷走整个 dist 文件夹）

## 构建（MSYS2 UCRT64）

```bash
pacman -S mingw-w64-ucrt-x86_64-qt6-multimedia   # 一次性环境准备
PATH="/d/Library/msys64/ucrt64/bin:$PATH" cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
PATH="/d/Library/msys64/ucrt64/bin:$PATH" cmake --build build
```

## 重新打包 dist

```bash
cp build/soundbutton.exe dist/
PATH="/d/Library/msys64/ucrt64/bin:$PATH" windeployqt --release --no-compiler-runtime dist/soundbutton.exe
# 再按 dist 内 DLL 的递归依赖，从 ucrt64/bin 复制缺失的非 Qt DLL（avcodec、libstdc++ 等）
```

## 代码结构

```
src/main.cpp              入口：起 QApplication、显示窗口、进事件循环
src/SoundButtonWidget.*   悬浮按钮窗口：emoji 按钮与图钉的绘制/命中、按下播放、拖动、右键菜单、托盘、预热调度
src/SoundLibrary.*        音效列表：QAudioDecoder 预解码成内存 PCM、裁首尾静音、config.json 持久化
src/AudioEngine.*         QAudioSink 播放封装：按格式预热热流、suspend/resume 重播、零拷贝推流
src/LatencyLog.h          点击→推流的耗时打点（SOUNDBUTTON_LATENCY_LOG=1 时输出）
assets/                   界面素材（按钮 10 帧动画 + 图钉），编译进 exe，运行时不需要外部文件
tools/bake-button-frames.ps1         把 Lottie 动画烘焙成 assets/button_*.png
tools/gen-preview.ps1                生成顶部那张 preview.png
tools/gen-architecture-diagram.ps1   生成下面的架构图（docs/architecture.png）
```

## 架构

![架构图](docs/architecture.png)

分层与依赖方向、两条关键路径（启动预热 / 点击播放）、延迟预算、必须守住的不变量，
见 [ARCHITECTURE.md](ARCHITECTURE.md)。改代码前建议先过一遍。
