// stats.h - 帧时间统计（纯计算，无系统调用）
// 算法与商业监控软件一致：30 秒窗口内逐帧记录，1% low = 最慢 1% 帧的平均帧时间换算帧率。
#pragma once
#include <cstdint>
#include <cstddef>
#include <deque>
#include <vector>
#include <algorithm>
#include <cmath>

struct StatsSnapshot
{
    bool   valid      = false;   // 是否有有效数据（否则显示 --）
    double fps        = 0.0;     // 最近 1 秒平均帧率
    double low1       = 0.0;     // 30 秒窗口 1% low
    double frameMs    = 0.0;     // 最近 1 秒平均帧时间
    size_t sampleCount = 0;      // 窗口内样本数
    double windowSec  = 0.0;     // 窗口实际覆盖秒数
};

class FrameStats
{
public:
    void SetQpcFreq(int64_t f) { qpcFreq_ = (f > 0 ? f : 10000000); }

    void Reset()
    {
        frames_.clear();
        lastQpc_ = 0;
        curPid_ = 0;
    }

    // 每帧调用一次。pid 变化或长时间无帧视为中断，统计自动重来。
    void AddFrame(uint32_t pid, int64_t qpc)
    {
        if (pid != curPid_) { frames_.clear(); lastQpc_ = 0; curPid_ = pid; }
        if (lastQpc_ != 0) {
            double ms = double(qpc - lastQpc_) * 1000.0 / double(qpcFreq_);
            if (ms > 5000.0) {
                // 超过 5 秒没有新帧，之前的序列不再连续，重新起算
                frames_.clear();
            } else if (ms > 0.0) {
                frames_.push_back({ qpc, (float)ms });
            }
        }
        lastQpc_ = qpc;
    }

    StatsSnapshot Compute(int64_t nowQpc)
    {
        StatsSnapshot s;
        if (frames_.empty() || lastQpc_ == 0) return s;

        const double nowMs = double(nowQpc - lastQpc_) * 1000.0 / double(qpcFreq_);
        if (nowMs > 1500.0) {                 // 1.5 秒没有新帧，视为已停止呈现
            return s;
        }

        const int64_t windowTicks = int64_t(WINDOW_SEC * double(qpcFreq_));
        const int64_t cutoff = nowQpc - windowTicks;
        while (!frames_.empty() && frames_.front().t < cutoff) frames_.pop_front();
        if (frames_.empty()) return s;

        // --- 最近 1 秒平均帧率：用帧时间累加，避免"数帧数"的窗口量化误差 ---
        double sumMs = 0.0;
        size_t n = 0;
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            sumMs += it->ms;
            ++n;
            if (sumMs >= 1000.0) break;
        }
        if (n == 0 || sumMs <= 0.0) return s;
        s.fps = 1000.0 * double(n) / sumMs;
        s.frameMs = sumMs / double(n);

        // --- 1% low：窗口内最慢 1% 帧的平均帧时间，再换算帧率 ---
        size_t total = frames_.size();
        size_t worst = (size_t)std::ceil(double(total) * 0.01);
        if (worst < 1) worst = 1;
        if (worst > total) worst = total;

        tmp_.clear();
        tmp_.reserve(total);
        double totalMs = 0.0;
        for (auto& f : frames_) { tmp_.push_back(f.ms); totalMs += f.ms; }

        if (worst >= total) {
            s.low1 = totalMs > 0.0 ? 1000.0 * double(total) / totalMs : 0.0;
        } else {
            std::nth_element(tmp_.begin(), tmp_.begin() + (total - worst), tmp_.end());
            double wsum = 0.0;
            for (size_t i = total - worst; i < total; ++i) wsum += tmp_[i];
            double avg = wsum / double(worst);
            s.low1 = avg > 0.0 ? 1000.0 / avg : 0.0;
        }

        s.sampleCount = total;
        s.windowSec = totalMs > 0.0 ? totalMs / 1000.0 : 0.0;
        s.valid = true;
        return s;
    }

    size_t SampleCount() const { return frames_.size(); }

private:
    struct F { int64_t t; float ms; };
    static constexpr double WINDOW_SEC = 30.0;

    std::deque<F> frames_;
    std::vector<float> tmp_;
    int64_t lastQpc_ = 0;
    int64_t qpcFreq_ = 10000000;
    uint32_t curPid_ = 0;
};
