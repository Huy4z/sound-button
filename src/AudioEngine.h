#pragma once

#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QByteArray>
#include <QObject>
#include <memory>
#include <vector>

#include "LatencyLog.h"

// 低延迟播放引擎。
// 播放是"点击即出声"，所以点击路径上不能有任何建流/重置开销：
//  - 每种采样格式保留一条已初始化的"热"QAudioSink（prepare() 预热，成本移出点击路径）
//  - 播放中再次点击：suspend()（丢弃已排队音频）→ 换 PCM → resume()，实测约 1ms；
//    而 stop() + start() 会重建 WASAPI 流，实测各设备 13~125ms
//  - PCM 用 shared_ptr 共享，点击路径不做内存拷贝
class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);

    // 预热一条采样格式的播放流（建流 + 流初始化都在这步付掉）。
    // 幂等：同一格式重复调用只会碰到已有的热流，不会重建。
    void prepare(const QAudioFormat &format);
    // 播放整段 PCM：播放中再调 = suspend→换源→resume（丢弃上一段，从头播新的）；
    // 空闲/暂停态则直接 start。两条路径都不建流、不拷贝。
    // pressUs：鼠标按下的时刻（见 LatencyLog.h），仅用于诊断日志，传 0 表示非点击触发
    void play(const std::shared_ptr<QByteArray> &pcm, const QAudioFormat &format,
              qint64 pressUs = 0);
    // 立即停声但保留热流（内部用 suspend，不是 stop）——拖动取消误播时用
    void stop();
    void invalidateSinks();   // 输出设备变化：丢弃热流，下次使用时重建
    // 音量作用于所有热流；对之后新建的流同样生效（新建时会套用 m_volume）
    void setVolume(qreal volume);
    // 是否有热流正在出声（用于状态显示；静音停声后为 false）
    bool isPlaying() const;

signals:
    void errorOccurred(const QString &message);

private:
    // 一条已初始化的播放流 = 一个格式 + 一个输出设备 + 一个复用中的 QAudioSink。
    // 流的生命周期与 WASAPI 流绑定：不销毁就一直保持初始化状态（这是低延迟的来源）。
    struct Stream {
        QAudioFormat format;              // 流的采样格式（热流按它区分）
        QByteArray deviceId;              // 建流时用的输出设备；设备变了这条流就作废
        std::unique_ptr<QAudioSink> sink;
        std::unique_ptr<QBuffer> buffer;
        QByteArray silence;               // 预热数据：buffer 引用它，须同生命周期
        std::shared_ptr<QByteArray> pcm;  // 播放中的 PCM：条目被删也保活
        qint64 lastUsedUs = 0;            // 最近使用时刻，超过 kMaxStreams 时淘汰最旧的
        bool warmed = false;              // 是否已经起播过一次（未预热的流首次 start 很贵）
    };

    // 按格式取热流；找不到且 createIfMissing=true 时新建（并做 LRU 淘汰）。
    // 返回的指针归 m_streams 所有，调用方不得保存超过一次播放的时间。
    Stream *streamFor(const QAudioFormat &format, bool createIfMissing);
    // 用几毫秒静音把流"跑"起来，把 WASAPI 初始化成本提前吃掉
    void warmUp(Stream &stream);
    // 销毁全部热流（析构 QAudioSink 即关闭底层 WASAPI 流）
    void releaseStreams();
    void setStreamVolume(Stream &stream);

    // 热流池：按采样格式各一条，最多 kMaxStreams 条
    std::vector<std::unique_ptr<Stream>> m_streams;
    qreal m_volume = 0.9;
};
