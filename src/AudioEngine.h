#pragma once

#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QByteArray>
#include <QObject>
#include <memory>
#include <vector>

#include "LatencyLog.h"

/// 低延迟播放引擎（播放层：只认识 PCM 与采样格式，不认识音效列表）。
/// 播放是"点击即出声"，所以点击路径上不能有任何建流/重置开销：
///  - 每种采样格式保留一条已初始化的"热"QAudioSink（prepare() 预热，成本移出点击路径）
///  - 播放中再次点击：suspend()（丢弃已排队音频）→ 换 PCM → resume()，实测约 1ms；
///    而 stop() + start() 会重建 WASAPI 流，实测各设备 13~125ms
///  - PCM 用 shared_ptr 共享，点击路径不做内存拷贝
class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);

    /// 预热一条采样格式的播放流（建流 + 流初始化都在这步付掉）。
    /// 幂等：同一格式重复调用只会命中已有热流，不会重建。
    void prepare(const QAudioFormat &format);

    /// 播放整段 PCM（点击路径上唯一的入口）。
    /// 播放中再调 = suspend → 换源 → resume（丢弃上一段，从头播新的）；空闲/暂停态则直接 start。
    /// 两条路径都不建流、不拷贝。
    /// \param pressUs 鼠标按下时刻（见 LatencyLog.h），仅用于诊断日志；0 = 非点击触发
    void play(const std::shared_ptr<QByteArray> &pcm, const QAudioFormat &format,
              qint64 pressUs = 0);

    /// 立即停声但保留热流（内部用 suspend，不是 stop）——拖动取消误播时用
    void stop();

    /// 输出设备变化：丢弃全部热流，下次使用时在新设备上重建
    void invalidateSinks();

    /// 音量（0~1）作用于所有热流；对之后新建的流同样生效（新建时会套用 m_volume）
    void setVolume(qreal volume);

private:
    /// 一条已初始化的播放流 = 一个格式 + 一个输出设备 + 一个复用中的 QAudioSink。
    /// 流的生命周期与 WASAPI 流绑定：不销毁就一直保持初始化状态（这是低延迟的来源）。
    struct Stream {
        QAudioFormat format;              ///< 流的采样格式（热流按它区分）
        QByteArray deviceId;              ///< 建流时用的输出设备；设备变了这条流就作废
        std::unique_ptr<QAudioSink> sink; ///< 已初始化的 sink（不销毁就一直是"热"的）
        std::unique_ptr<QBuffer> buffer;  ///< 把 PCM 包成 QIODevice 给 sink 读；只引用，不复制
        QByteArray silence;               ///< 预热数据：buffer 引用它，须同生命周期
        std::shared_ptr<QByteArray> pcm;  ///< 播放中的 PCM：条目被删也保活
        qint64 lastUsedUs = 0;            ///< 最近使用时刻；超过 kMaxStreams 时淘汰最旧的
        bool warmed = false;              ///< 已经起播过一次（未预热的流首次 start 很贵）
    };

    /// 按格式取热流；没有就新建（先按 LRU 淘汰最久未用的）。
    /// 返回的指针归 m_streams 所有，调用方不得保存超过一次播放的时间。
    Stream *streamFor(const QAudioFormat &format);

    /// 用几毫秒静音把流"跑"起来，把 WASAPI 初始化成本提前吃掉（见 kSilenceMs）
    void warmUp(Stream &stream);

    /// 把当前音量套到某条流上（值没变就不惊动后端）
    void setStreamVolume(Stream &stream);

    std::vector<std::unique_ptr<Stream>> m_streams; ///< 热流池：按采样格式各一条，上限 kMaxStreams
    qreal m_volume = 0.9;                           ///< 音量 0~1；新建流时套用
};
