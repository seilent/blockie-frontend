#pragma once
#include <cstdint>
#include <string>

namespace fd {

struct T9Pose {
    int shift = 0;
    int a256 = 256;
};

class T9 {
public:
    void set(const std::string& s);
    void change(const std::string& s, int64_t now);
    void land();
    T9Pose pose(int64_t now) const;
    const std::string& shown(int64_t now) const;
    bool running(int64_t now) const { return now < end(); }
    int64_t end() const;

    static int out_ms();
    static int in_ms();
    static int out_px();
    static int in_px();

private:
    enum class Phase { Idle, Out, In };

    void advance(int64_t now);

    std::string shown_;
    std::string pending_;
    Phase phase_ = Phase::Idle;
    int64_t t0_ = 0;
    T9Pose from_;
};

}
