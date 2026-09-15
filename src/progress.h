#pragma once
#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace gibbon {
struct Progress {
    std::string stage, detail;
    double elapsedMs = 0;
    bool cached = false;
};
struct StageTiming {
    std::string stage, detail;
    double milliseconds = 0;
    bool cached = false;
};
using ProgressCallback = std::function<void(const Progress &)>;
// Sequential, exclusive wall-clock spans. Observer runs on the calling worker.
class ProcessingTrace {
    using Clock = std::chrono::steady_clock;
    Clock::time_point start = Clock::now(), boundary = start;
    ProgressCallback callback;
    Progress current;
    std::vector<StageTiming> spans;
  public:
    explicit ProcessingTrace(ProgressCallback callback = {}) : callback(std::move(callback)) {}
    Progress state() const { return current; }
    double elapsed() const { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
    void stage(std::string name, std::string detail = {}, bool cached = false) {
        auto now = Clock::now();
        if (!current.stage.empty())
            spans.push_back({current.stage, current.detail,
                std::chrono::duration<double, std::milli>(now - boundary).count(), current.cached});
        boundary = now;
        current = {std::move(name), std::move(detail), elapsed(), cached};
        if (callback && !current.stage.empty()) callback(current);
    }
    std::vector<StageTiming> finish() { stage({}); return spans; }
};
}
