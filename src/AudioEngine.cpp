#include "AudioEngine.h"

#include <QAudio>
#include <QAudioSink>
#include <QBuffer>
#include <QMediaDevices>

AudioEngine::AudioEngine(QObject *parent) : QObject(parent) {
    m_buffer = std::make_unique<QBuffer>();
}

void AudioEngine::ensureSink(const QAudioFormat &format) {
    if (m_sink && m_sinkFormat == format) return;
    m_sink = std::make_unique<QAudioSink>(QMediaDevices::defaultAudioOutput(), format);
    m_sinkFormat = format;
    if (m_sink->error() != QAudio::NoError) {
        emit errorOccurred(QStringLiteral("无法打开音频输出设备"));
        m_sink.reset();
        m_sinkFormat = QAudioFormat();
    }
}

void AudioEngine::play(const QByteArray &pcm, const QAudioFormat &format) {
    if (pcm.isEmpty() || !format.isValid()) return;
    if (m_sink) m_sink->stop();      // 播放中再点 = 停止并从头重播
    ensureSink(format);
    if (!m_sink) return;

    m_buffer->close();
    m_data = pcm;
    m_buffer->setBuffer(&m_data);
    m_buffer->open(QIODevice::ReadOnly);
    m_buffer->seek(0);

    m_sink->setVolume(m_volume);
    m_sink->start(m_buffer.get());
    if (m_sink->error() != QAudio::NoError)
        emit errorOccurred(QStringLiteral("音频播放启动失败"));
}

void AudioEngine::stop() {
    if (m_sink) m_sink->stop();
}

void AudioEngine::setVolume(qreal volume) {
    m_volume = qBound<qreal>(0.0, volume, 1.0);
    if (m_sink) m_sink->setVolume(m_volume);
}

bool AudioEngine::isPlaying() const {
    return m_sink && m_sink->state() == QAudio::ActiveState;
}
