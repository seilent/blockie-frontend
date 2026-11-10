#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fd {

enum class PadAction { Up, Down, Left, Right, Accept, Back, Context, Prev, Next, Start };

struct PadEdge {
    PadAction action;
    bool down;
};

enum class Held { No, Yes, Unknown };

struct PadId {
    uint16_t bus = 0;
    uint16_t vendor = 0;
    uint16_t product = 0;
    std::string phys;
    std::string name;
};

bool pad_matches(const PadId& id);
PadId pad_id(int fd);

class PadReader {
public:
    PadReader() = default;
    ~PadReader();
    PadReader(const PadReader&) = delete;
    PadReader& operator=(const PadReader&) = delete;

    bool open();
    void adopt_fd(int fd);
    bool is_open() const { return fd_ >= 0; }
    int fd() const { return fd_; }
    void close();

    std::vector<PadAction> poll();
    std::vector<PadEdge> poll_edges();
    Held held(PadAction dir) const;

private:
    void resync(std::vector<PadEdge>& out);

    int fd_ = -1;
    int hat_x_ = 0;
    int hat_y_ = 0;
    bool dpad_[4] = {false, false, false, false};
    bool dropping_ = false;
};

}
