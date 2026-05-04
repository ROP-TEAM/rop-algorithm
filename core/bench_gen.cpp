#define _USE_MATH_DEFINES
#include "bench_gen.h"
#include <random>
#include <cmath>
#include <vector>
#include <utility>

static const char* const TAGS[] = {"fragile", "heavy", "express"};
static constexpr int TAG_N = 3;

static double haversine(double lat1, double lon1, double lat2, double lon2) {
    double dlat = (lat2 - lat1) * M_PI / 180.0;
    double dlon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dlat / 2) * sin(dlat / 2)
             + cos(lat1 * M_PI / 180) * cos(lat2 * M_PI / 180)
             * sin(dlon / 2) * sin(dlon / 2);
    return 2.0 * 6371000.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

using Pos = std::pair<double, double>;

static std::vector<Pos> uniformPositions(const BenchConfig& c, std::mt19937& rng) {
    std::uniform_real_distribution<double> latd(c.depot_lat - c.radius_deg,
                                                c.depot_lat + c.radius_deg);
    std::uniform_real_distribution<double> lond(c.depot_lon - c.radius_deg,
                                                c.depot_lon + c.radius_deg);
    std::vector<Pos> pos(c.num_orders);
    for (auto& p : pos) p = {latd(rng), lond(rng)};
    return pos;
}

static std::vector<Pos> clusteredPositions(const BenchConfig& c, std::mt19937& rng) {
    std::uniform_real_distribution<double> clatd(c.depot_lat - c.radius_deg,
                                                 c.depot_lat + c.radius_deg);
    std::uniform_real_distribution<double> clond(c.depot_lon - c.radius_deg,
                                                 c.depot_lon + c.radius_deg);
    std::vector<Pos> centers(c.num_clusters);
    for (auto& ct : centers) ct = {clatd(rng), clond(rng)};

    std::uniform_int_distribution<int> cd(0, c.num_clusters - 1);
    double sp = c.cluster_spread_deg;
    std::vector<Pos> pos(c.num_orders);
    for (auto& p : pos) {
        auto [clat, clon] = centers[cd(rng)];
        std::uniform_real_distribution<double> slatd(clat - sp, clat + sp);
        std::uniform_real_distribution<double> slond(clon - sp, clon + sp);
        p = {slatd(rng), slond(rng)};
    }
    return pos;
}

static void addNodes(solver::SolveRequest& req, const BenchConfig& c,
                     std::vector<double>& lats, std::vector<double>& lons,
                     std::mt19937& rng) {
    auto pos = (c.num_clusters > 1) ? clusteredPositions(c, rng)
                                    : uniformPositions(c, rng);
    std::uniform_int_distribution<int> demd(c.demand_min, c.demand_max);
    std::uniform_int_distribution<int> tagd(0, TAG_N);
    std::uniform_int_distribution<int> twsd(480, 540);
    int wlo = c.enable_tw ? c.tw_width_min : 1;
    int whi = c.enable_tw ? std::max(c.tw_width_max, wlo) : 1;
    std::uniform_int_distribution<int> twwd(wlo, whi);

    for (int i = 0; i < c.num_orders; ++i) {
        auto [lat, lon] = pos[i];
        lats.push_back(lat);
        lons.push_back(lon);
        auto* pn = req.add_nodes();
        pn->set_id("ORD-" + std::to_string(i + 1));
        pn->set_lat(lat);
        pn->set_lng(lon);
        pn->set_demand(demd(rng));
        pn->set_service_time(10);
        pn->set_type("delivery");
        int ti = tagd(rng);
        if (ti < TAG_N) pn->add_tags(TAGS[ti]);
        if (c.enable_tw) {
            int s = twsd(rng);
            pn->set_tw_start(s);
            pn->set_tw_end(s + twwd(rng));
        }
    }
}

static void addVehicles(solver::SolveRequest& req, const BenchConfig& c,
                        std::mt19937& rng) {
    std::uniform_int_distribution<int> capd(c.cap_min, c.cap_max);
    std::bernoulli_distribution        tagc(c.tag_coverage);
    for (int v = 0; v < c.num_vehicles; ++v) {
        auto* pv = req.add_vehicles();
        pv->set_id("V-" + std::to_string(v + 1));
        pv->set_capacity(capd(rng));
        pv->set_shift_start(480);
        pv->set_shift_end(1080);
        pv->set_max_tasks(0);
        if (v < (int)(c.num_vehicles * c.max_dist_frac))
            pv->set_max_distance(c.max_dist_m);
        for (int t = 0; t < TAG_N; ++t)
            if (tagc(rng)) pv->add_tags(TAGS[t]);
    }
}

static void addMatrices(solver::SolveRequest& req,
                        const std::vector<double>& lats,
                        const std::vector<double>& lons) {
    int n = (int)lats.size();
    req.set_matrix_size(n);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            double d = (i == j) ? 0.0
                                : haversine(lats[i], lons[i], lats[j], lons[j]);
            req.add_distances(d);
            req.add_durations(d / 40000.0 * 60.0);  // 40 km/h → minutes
        }
}

solver::SolveRequest buildRequest(const BenchConfig& c) {
    std::mt19937 rng(c.seed);
    solver::SolveRequest req;
    req.mutable_depot()->set_id("depot");
    req.mutable_depot()->set_lat(c.depot_lat);
    req.mutable_depot()->set_lng(c.depot_lon);
    std::vector<double> lats = {c.depot_lat};
    std::vector<double> lons = {c.depot_lon};
    addNodes(req, c, lats, lons, rng);
    addVehicles(req, c, rng);
    addMatrices(req, lats, lons);
    return req;
}
