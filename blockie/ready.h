#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "children.h"

namespace fd {

class ReadyReport {
public:
    ReadyReport(Children& kids, std::function<int64_t()> now_ms, std::string tool = "/usr/bin/blockie-session");
    void posted(bool pad_open, bool activated);
    void shown(bool pad_open, bool activated);
    bool wants_frame(bool pad_open, bool activated) const;
    void tick();
    int64_t deadline() const;
    int status() const { return status_; }

private:
    void spawn();
    void ended(int raw);
    void failed();

    Children& kids_;
    std::function<int64_t()> now_;
    std::string tool_;
    std::shared_ptr<int> life_ = std::make_shared<int>(0);
    bool armed_ = false, dropped_ = false, started_ = false, finished_ = false;
    int attempts_ = 0, status_ = -1;
    int64_t retry_at_ = -1;
};

}
