#pragma once
#include <QByteArray>
#include <list>
#include <optional>
#include <variant>
#include <vector>
#include <opencv2/core.hpp>

namespace gibbon {
// Owned values only: clone matrices at both boundaries. Used on the engine worker.
class ProcessingCache {
  public:
    using Value = std::variant<cv::Mat, std::vector<cv::Rect>>;
    struct Stats { size_t bytes = 0, hits = 0, misses = 0, evictions = 0; };
    explicit ProcessingCache(size_t limit = 128 * 1024 * 1024) : limit(limit) {}
    std::optional<Value> get(const QByteArray &key) {
        for (auto it = entries.begin(); it != entries.end(); ++it)
            if (it->key == key) {
                auto value = copy(it->value);
                entries.splice(entries.begin(), entries, it);
                ++stats.hits;
                return value;
            }
        ++stats.misses;
        return {};
    }
    void put(const QByteArray &key, const Value &value) {
        const size_t bytes = sizeof(Entry) + size_t(key.size()) +
            (std::holds_alternative<cv::Mat>(value)
                ? std::get<cv::Mat>(value).total() * std::get<cv::Mat>(value).elemSize()
                : std::get<std::vector<cv::Rect>>(value).size() * sizeof(cv::Rect));
        if (bytes > limit) return;
        for (auto it = entries.begin(); it != entries.end(); ++it)
            if (it->key == key) {
                stats.bytes -= it->bytes;
                entries.erase(it);
                break;
            }
        while (stats.bytes + bytes > limit && !entries.empty()) {
            stats.bytes -= entries.back().bytes;
            entries.pop_back();
            ++stats.evictions;
        }
        entries.push_front({key, copy(value), bytes});
        stats.bytes += bytes;
    }
    void clear() { entries.clear(); stats.bytes = 0; }
    Stats statistics() const { return stats; }
  private:
    struct Entry { QByteArray key; Value value; size_t bytes; };
    static Value copy(const Value &value) {
        if (auto mat = std::get_if<cv::Mat>(&value)) return mat->clone();
        return std::get<std::vector<cv::Rect>>(value);
    }
    size_t limit;
    Stats stats;
    std::list<Entry> entries;
};
}
