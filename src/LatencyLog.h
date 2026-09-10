#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QtGlobal>
#include <cstdio>

// 点击→出声的延迟诊断（默认关闭，避免给点击路径加任何开销）：
//   set SOUNDBUTTON_LATENCY_LOG=1 && soundbutton.exe 2> log.txt
// 直接写 stderr 而不是 qInfo()：GUI 子系统的程序里 Qt 默认把日志送去调试器，
// 只有这样才能稳定地重定向到文件/终端。
// 用法约定：只允许在"动作做完之后"打点（例如推流结束后），
// 别把 fprintf 插在鼠标按下与推流之间，否则测到的是日志 I/O 的耗时。
inline bool sbLatencyLog() {
    // 环境变量只读一次：点击路径上不能有反复读环境变量的开销
    static const bool on = !qEnvironmentVariableIsEmpty("SOUNDBUTTON_LATENCY_LOG");
    return on;
}

// 进程内单调时基（微秒，从首次调用起算，不受系统时间调整影响），
// 用于跨模块打点：鼠标按下记一次、推流结束再记一次，相减就是端到端耗时。
inline qint64 sbNowUs() {
    static const QElapsedTimer t = [] { QElapsedTimer x; x.start(); return x; }();
    return t.nsecsElapsed() / 1000;
}

// 输出一行带时间戳的日志。必须 fflush：stderr 被重定向到文件时是带缓冲的，
// 程序若被强杀，没 flush 的日志会整段丢失（调试时会被误导）。
inline void sbLatLog(const QString &line) {
    if (!sbLatencyLog()) return;
    fprintf(stderr, "[延迟 %8.2fms] %s\n", sbNowUs() / 1000.0,
            line.toUtf8().constData());
    fflush(stderr);
}
