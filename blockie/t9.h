#pragma once
#include <cstdint>
#include <string>

namespace fd {

struct T9Pose {
    int shift = 0;
    int a256 = 256;
};

struct T9Text {
    int64_t id = 0;
    std::string title;
    std::string system;
    std::string next;

    bool operator==(const T9Text& o) const
    {
        return id == o.id && title == o.title && system == o.system && next == o.next;
    }
    bool operator!=(const T9Text& o) const { return !(*this == o); }
};

class T9 {
public:
    void set(const T9Text& s);
    void change(const T9Text& s, int64_t now);
    void land();
    T9Pose pose(int64_t now) const;
    const T9Text& shown(int64_t now) const;
    bool running(int64_t now) const { return now < end(); }
    int64_t end() const;

    static int out_ms();
    static int in_ms();
    static int out_px();
    static int in_px();

private:
    enum class Phase { Idle, Out, In };

    void advance(int64_t now);

    T9Text shown_;
    T9Text pending_;
    Phase phase_ = Phase::Idle;
    int64_t t0_ = 0;
    T9Pose from_;
};

}
