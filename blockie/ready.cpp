#include "ready.h"

#include <cstdio>
#include <utility>

#include <sys/types.h>
#include <sys/wait.h>

namespace fd {

namespace {

constexpr int64_t kRetryMs[] = {2000, 5000, 10000, 20000};
constexpr int kRetries = (int)(sizeof kRetryMs / sizeof kRetryMs[0]);

}

ReadyReport::ReadyReport(Children& kids, std::function<int64_t()> now_ms, std::string tool)
    : kids_(kids), now_(std::move(now_ms)), tool_(std::move(tool))
{
}

void ReadyReport::posted(bool pad_open, bool activated)
{
    if (started_)
        return;
    armed_ = pad_open && activated;
    dropped_ = !armed_;
}

void ReadyReport::shown(bool pad_open, bool activated)
{
    if (started_ || !armed_)
        return;
    armed_ = false;
    if (!pad_open || !activated) {
        dropped_ = true;
        return;
    }
    started_ = true;
    spawn();
}

bool ReadyReport::wants_frame(bool pad_open, bool activated) const
{
    return dropped_ && pad_open && activated && !armed_ && !started_;
}

void ReadyReport::tick()
{
    if (finished_ || retry_at_ < 0 || now_() < retry_at_)
        return;
    spawn();
}

int64_t ReadyReport::deadline() const { return retry_at_; }

void ReadyReport::spawn()
{
    attempts_++;
    retry_at_ = -1;
    std::fprintf(stderr, "blockie-frontend: ready report\n");
    std::weak_ptr<int> alive = life_;
    pid_t pid = kids_.spawn({tool_, "ready"}, [this, alive](int status) {
        if (!alive.expired())
            ended(status);
    });
    if (pid < 0) {
        std::fprintf(stderr, "blockie-frontend: ready report did not start\n");
        failed();
    }
}

void ReadyReport::ended(int raw)
{
    status_ = raw;
    if (WIFEXITED(raw) && WEXITSTATUS(raw) == 0) {
        finished_ = true;
        std::fprintf(stderr, "blockie-frontend: ready report ok\n");
        return;
    }
    if (WIFSIGNALED(raw))
        std::fprintf(stderr, "blockie-frontend: ready report failed (signal %d)\n", WTERMSIG(raw));
    else
        std::fprintf(stderr, "blockie-frontend: ready report failed (exit %d)\n", WEXITSTATUS(raw));
    failed();
}

void ReadyReport::failed()
{
    if (attempts_ > kRetries) {
        finished_ = true;
        std::fprintf(stderr, "blockie-frontend: ready report given up\n");
        return;
    }
    int64_t ms = kRetryMs[attempts_ - 1];
    retry_at_ = now_() + ms;
    std::fprintf(stderr, "blockie-frontend: ready report retry in %lld s\n", (long long)(ms / 1000));
}

}
