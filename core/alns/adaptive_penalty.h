#pragma once

#include <algorithm>

namespace hfvrptwb {
namespace alns {

class AdaptivePenalty {
    double cap_pen_ = 1000.0;
    double tw_pen_ = 1000.0;
    double overtime_pen_ = 500.0;
    static constexpr double TARGET = 0.20;

public:
    void update(double feasible_ratio) {
        double factor = (feasible_ratio < TARGET - 0.05) ? 1.2
                      : (feasible_ratio > TARGET + 0.05) ? 0.85
                      : 1.0;
        cap_pen_ = std::clamp(cap_pen_ * factor, 50.0, 1e6);
        tw_pen_  = std::clamp(tw_pen_  * factor, 50.0, 1e6);
        overtime_pen_ = std::clamp(overtime_pen_ * factor, 25.0, 1e6);
    }

    double capacity()    const { return cap_pen_; }
    double timeWindow()  const { return tw_pen_; }
    double overtime()    const { return overtime_pen_; }
    double coefficient() const { return cap_pen_; }
};

} // namespace alns
} // namespace hfvrptwb
