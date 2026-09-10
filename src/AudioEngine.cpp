#include "AudioEngine.h"

#include <QAudio>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QString>
#include <algorithm>

namespace {
constexpr int kMaxStreams = 8;   // 最多保留几条热流（按采样格式区分，超出则淘汰最久未用的）
constexpr int kSilenceMs = 5;    // 预热时先喂一段静音，把 WASAPI 流初始化成本提前吃掉

// 把 QAudio::State 翻成中文，只用于诊断日志
const char *stateName(QAudio::State state) {
    switch (state) {
    case QAudio::ActiveState:    return "播放中";
    case QAudio::SuspendedState: return "已暂停";
    case QAudio::IdleState:      return "空闲(流已初始化)";
    case QAudio::StoppedState:   return "已停止(流未初始化)";
    }
    return "?";
}
}  // namespace

AudioEngine::AudioEngine(QObject *parent) : QObject(parent) {
    // 插拔耳机 / 切换默认输出后，热流绑定的旧设备已失效，丢弃重建
    auto *devices = new QMediaDevices(this);
    connect(devices, &QMediaDevices::audioOutputsChanged, this,
            &AudioEngine::invalidateSinks);
}

AudioEngine::Stream *AudioEngine::streamFor(const QAudioFormat &format,
                                            bool createIfMissing) {
    // 每次取流都问一次默认设备：用户换输出设备后，旧设备上的热流已经没用了。
    // 这次调用是廉价的（设备列表已缓存），昂贵的首次枚举在 prepare() 阶段就付过了。
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    const QByteArray deviceId = device.id();

    // 默认设备变了：旧设备上的热流直接丢弃（下面会按需重建到新设备）
    m_streams.erase(std::remove_if(m_streams.begin(), m_streams.end(),
                                   [&deviceId](const std::unique_ptr<Stream> &s) {
                                       return s->deviceId != deviceId;
                                   }),
                    m_streams.end());

    // 命中热流：只更新使用时间，不碰 sink，这是点击路径上最理想的情况
    for (const auto &s : m_streams) {
        if (s->format == format) {
            s->lastUsedUs = sbNowUs();
            return s.get();
        }
    }
    if (!createIfMissing) return nullptr;

    if (int(m_streams.size()) >= kMaxStreams) {   // 超出上限淘汰最久未用的
        m_streams.erase(std::min_element(
            m_streams.begin(), m_streams.end(),
            [](const auto &a, const auto &b) { return a->lastUsedUs < b->lastUsedUs; }));
    }

    auto stream = std::make_unique<Stream>();
    stream->format = format;
    stream->deviceId = deviceId;
    stream->sink = std::make_unique<QAudioSink>(device, format);
    // isNull()：设备/格式组合无效；error()：驱动拒绝。两者都当建流失败处理
    if (stream->sink->isNull() || stream->sink->error() != QAudio::NoError) {
        emit errorOccurred(QStringLiteral("无法打开音频输出设备"));
        return nullptr;
    }
    // QBuffer 只是把已有 PCM 包成 QIODevice 给 sink 读，不复制数据
    stream->buffer = std::make_unique<QBuffer>();
    stream->lastUsedUs = sbNowUs();
    m_streams.push_back(std::move(stream));
    return m_streams.back().get();
}

// 预热：用几毫秒静音起播一次。WASAPI 流的初始化（各设备实测 13~125ms）只在这里付一次，
// 之后空闲态起播 ≈0.1ms、播放中重播 ≈1ms，点击路径上不再有建流开销。
void AudioEngine::warmUp(Stream &stream) {
    stream.warmed = true;
    // 静音长度取 5ms：够触发一次真实的流初始化，又短到几乎"瞬间播完"转入 Idle
    const int frames = qMax(1, stream.format.sampleRate() * kSilenceMs / 1000);
    stream.silence = QByteArray(qsizetype(frames) * stream.format.bytesPerFrame(), '\0');
    stream.buffer->close();
    stream.buffer->setBuffer(&stream.silence);
    stream.buffer->open(QIODevice::ReadOnly);
    setStreamVolume(stream);
    stream.sink->start(stream.buffer.get());
    if (sbLatencyLog())
        sbLatLog(QStringLiteral("预热 %1Hz/%2ch 完成（%3ms 静音，静音会立即播完转空闲）")
                     .arg(stream.format.sampleRate())
                     .arg(stream.format.channelCount())
                     .arg(kSilenceMs));
}

void AudioEngine::prepare(const QAudioFormat &format) {
    if (!format.isValid()) return;
    Stream *stream = streamFor(format, true);
    // 只预热没跑过的流；已热的不动，避免把正在播放的流打断
    if (stream && !stream->warmed) warmUp(*stream);
}

void AudioEngine::play(const std::shared_ptr<QByteArray> &pcm, const QAudioFormat &format,
                       qint64 pressUs) {
    if (!pcm || pcm->isEmpty() || !format.isValid()) return;
    const qint64 t0 = sbNowUs();
    // 第一步：拿到目标格式的热流（正常情况下就是一次线性查找）
    Stream *stream = streamFor(format, true);
    if (!stream) return;

    QAudioSink *sink = stream->sink.get();
    // 播放中/已暂停都算"流已经跑起来了"，可以走便宜的 suspend→resume 路径
    const QAudio::State before = sink->state();
    const bool wasRunning =
        before == QAudio::ActiveState || before == QAudio::SuspendedState;

    // 第二步：换数据源。只做指针替换 + QBuffer 重新 open/seek，不复制 PCM 字节
    // 零拷贝换源：PCM 由 stream 与音效条目共享持有，播放中删除条目也不会悬空
    stream->pcm = pcm;
    stream->buffer->close();
    stream->buffer->setBuffer(stream->pcm.get());
    stream->buffer->open(QIODevice::ReadOnly);
    stream->buffer->seek(0);

    // 第三步：起播。wasRunning 时 suspend 会丢掉还没播完的旧数据，
    // resume 后后端从新 buffer 的当前位置继续取——所以听到的一定是新音效的开头。
    // 播放中再点：suspend() 会丢弃已排队的旧音频，恢复后直接从新数据开始
    if (wasRunning) sink->suspend();
    setStreamVolume(*stream);
    if (wasRunning) sink->resume(); else sink->start(stream->buffer.get());
    stream->warmed = true;

    if (sink->error() != QAudio::NoError) {
        emit errorOccurred(QStringLiteral("音频播放启动失败"));
        return;
    }
    // 诊断日志放在所有工作完成之后，避免 I/O 混进被测区间
    if (sbLatencyLog()) {
        const double playMs = (sbNowUs() - t0) / 1000.0;
        sbLatLog(QStringLiteral("推流 %1ms 路径=%2(%3→%4) 音频=%5ms%6")
                     .arg(playMs, 0, 'f', 3)
                     .arg(wasRunning ? QStringLiteral("suspend→resume") : QStringLiteral("start"))
                     .arg(QString::fromUtf8(stateName(before)))
                     .arg(QString::fromUtf8(stateName(sink->state())))
                     .arg(1000.0 * stream->pcm->size() /
                              (format.bytesPerFrame() * format.sampleRate()),
                          0, 'f', 1)
                     .arg(pressUs > 0
                              ? QStringLiteral("  按下→推流 %1ms")
                                    .arg((sbNowUs() - pressUs) / 1000.0, 0, 'f', 3)
                              : QString()));
    }
}

void AudioEngine::stop() {
    // 用 suspend 而不是 stop：停声即时生效，但保住已初始化的流（stop 后需重建，很贵）
    for (const auto &stream : m_streams)
        if (stream->sink && stream->sink->state() == QAudio::ActiveState)
            stream->sink->suspend();
}

void AudioEngine::invalidateSinks() { releaseStreams(); }

void AudioEngine::releaseStreams() {
    m_streams.clear();   // 析构 QAudioSink 会关闭对应的 WASAPI 流
}

void AudioEngine::setStreamVolume(Stream &stream) {
    if (!stream.sink) return;
    // setVolume 会触发一次后端调用，值没变就别惊动它
    if (!qFuzzyCompare(stream.sink->volume() + 1.0, m_volume + 1.0))
        stream.sink->setVolume(m_volume);
}

void AudioEngine::setVolume(qreal volume) {
    m_volume = qBound<qreal>(0.0, volume, 1.0);
    for (const auto &stream : m_streams) setStreamVolume(*stream);
}

bool AudioEngine::isPlaying() const {
    // 任一热流在出声就算"正在播放"（同一时刻通常只有一条流是活的）
    for (const auto &stream : m_streams)
        if (stream->sink && stream->sink->state() == QAudio::ActiveState) return true;
    return false;
}
