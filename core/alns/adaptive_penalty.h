#pragma once

#include <algorithm>

namespace hfvrptwb {
namespace alns {

class AdaptivePenalty {
    double cap_pen_ = 1000.0;
    double tw_pen_ = 1000.0;
    double overtime_pen_ = 500.0;
    double min_cap_ = 100.0;
    double min_tw_ = 100.0;
    double min_ot_ = 50.0;
    static constexpr double TARGET = 0.20;
    static constexpr double CLAMP_DECAY = 0.98;
    static constexpr double MIN_CAP_FLOOR = 10.0;
    static constexpr double MIN_TW_FLOOR = 10.0;
    static constexpr double MIN_OT_FLOOR = 5.0;

public:
    void update(double feasible_ratio) {
        double factor = (feasible_ratio < TARGET - 0.05) ? 1.2
                      : (feasible_ratio > TARGET + 0.05) ? 0.85
                      : 1.0;
        cap_pen_ = std::clamp(cap_pen_ * factor, min_cap_, 1e6);
        tw_pen_  = std::clamp(tw_pen_  * factor, min_tw_, 1e6);
        overtime_pen_ = std::clamp(overtime_pen_ * factor, min_ot_, 1e6);

        min_cap_ = std::max(MIN_CAP_FLOOR, min_cap_ * CLAMP_DECAY);
        min_tw_  = std::max(MIN_TW_FLOOR,  min_tw_  * CLAMP_DECAY);
        min_ot_  = std::max(MIN_OT_FLOOR,  min_ot_  * CLAMP_DECAY);
    }

    double capacity()    const { return cap_pen_; }
    double timeWindow()  const { return tw_pen_; }
    double overtime()    const { return overtime_pen_; }
    double coefficient() const { return cap_pen_; }
};

} // namespace alns
} // namespace hfvrptwb
