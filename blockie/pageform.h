#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../gfx/canvas.h"
#include "engine.h"
#include "motion.h"

namespace fd {

struct PageInk {
    Rect r;
    eng::Ink ink = eng::Ink::Text;
};

struct PageRow {
    std::string name;
    Rect r;
    eng::Ink ink = eng::Ink::Text;
    bool pressed = false;
};

struct PageHead {
    std::string word;
    std::vector<PageRow> letters;
    std::vector<PageRow> tabs;
    std::vector<int> rank;
    bool layer = false;
};

struct PageForm {
    bool closing = false;
    std::vector<PageInk> under;
    std::vector<PageInk> over;
    std::vector<PageRow> under_els;
    std::vector<PageRow> over_els;
    PageHead under_head;
    PageHead over_head;
};

struct PageRun {
    PageForm form;
    eng::Instance* in = nullptr;
    int64_t t0 = -1;
    std::vector<Surface> src;
    Surface words[2];
    eng::Frame frame;
    std::vector<PageRow> rows;
    int pair = 0;
};

Rect page_art(int split, bool band);
std::vector<PageInk> page_inks(Rect art);
std::vector<PageRow> home_elements(int split, bool band, int pressed);
std::vector<PageRow> page_elements(const std::string& word, const char* head, const std::vector<PageRow>& rows);
bool head_side(const PageForm& f, bool& under);
const PageHead& form_head(const PageForm& f);
int64_t head_start3(int order);
std::vector<int> tab_ranks(const PageForm& f, const PageHead& h);
std::vector<Surface> page_sources(const Surface& a, const Surface& b, const Surface words[2]);
std::vector<int> part_bands(const PageForm& f);
int part_band(const PageForm& f, const PageRow& e);
int64_t part_start3(int band);
int64_t form_thirds(const PageForm& f);
int64_t form_ms(const PageForm& f);
int64_t form_duration(const PageForm& f);
int64_t page_time(const PageForm& f, int64_t ft);
int page_rest(const PageForm& f, int64_t ft, bool reversed, bool settled);
int page_sid(const PageRun& run);
void page_frame_at(const PageForm& f, int64_t ft, eng::Frame& fr);
eng::Frame page_frame(const PageForm& f, int64_t ft);
std::vector<std::string> page_block_names(const PageForm& f);
FoldPlay page_fold(const std::shared_ptr<PageRun>& run);

}
