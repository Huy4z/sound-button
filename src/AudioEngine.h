#pragma once

#include <QObject>
#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QByteArray>
#include <memory>

// 低延迟播放引擎：PCM 已在内存，play() 只做设备启动 + 推流，
// 复用同一 QAudioSink 避免每次点击重建设备的开销。
class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);

    void play(const QByteArray &pcm, const QAudioFormat &format);
    void stop();
    void setVolume(qreal volume);
    bool isPlaying() const;

signals:
    void errorOccurred(const QString &message);

private:
    void ensureSink(const QAudioFormat &format);

    std::unique_ptr<QAudioSink> m_sink;
    std::unique_ptr<QBuffer> m_buffer;
    QByteArray m_data;             // QBuffer 引用它的内容，必须与 sink 同生命周期
    QAudioFormat m_sinkFormat;
    qreal m_volume = 0.9;
};
