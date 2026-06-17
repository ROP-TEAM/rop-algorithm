"""Route-format proof: the same day's routes, current grouping vs geographic
re-grouping, so the deadhead reduction reported in REPORT.md can be inspected as
actual routes rather than a single number.

For one weekday we take every route due that day and build trucks two ways, with
identical nearest-neighbour sequencing on both sides:
  before — grouped by the current administrative vehicle (current_vehicle_id)
  after  — grouped by geography (k-means) into the SAME number of trucks

Each truck is emitted as an ordered list of route units with its own inter-stop
deadhead, alongside the day's totals. This is the assignment lever in isolation;
it does not yet enforce time windows or shift length (those are hard constraints
in the real solver), so the gain is an optimistic bound — same caveat as
8_proto_result.json.

Output: 9_proto_routes_day3.json
"""

import json
import math

import numpy as np
from scipy.cluster.vq import kmeans2

UNITS_FILE = "7_route_units.json"
OUTPUT_FILE = "9_proto_routes_day3.json"

EARTH_RADIUS_M = 6_371_000
DAY_INDEX = 3
DAY_NAME = "Wed"


def haversine_m(a, b):
    lat1, lng1 = math.radians(a[0]), math.radians(a[1])
    lat2, lng2 = math.radians(b[0]), math.radians(b[1])
    d_lat, d_lng = lat2 - lat1, lng2 - lng1
    h = math.sin(d_lat / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(d_lng / 2) ** 2
    return 2 * EARTH_RADIUS_M * math.asin(math.sqrt(h))


def nn_order(group):
    """Greedy end->nearest-start chain; returns the visiting order and its
    inter-stop deadhead in metres."""
    if len(group) <= 1:
        return list(group), 0.0
    remaining = list(group)
    current = remaining.pop(0)
    order = [current]
    total = 0.0
    while remaining:
        nxt = min(remaining, key=lambda u: haversine_m(current["end"], u["start"]))
        total += haversine_m(current["end"], nxt["start"])
        remaining.remove(nxt)
        order.append(nxt)
        current = nxt
    return order, total


def route_obj(truck_id, group):
    order, deadhead_m = nn_order(group)
    return {
        "truck": truck_id,
        "stops": len(order),
        "inter_stop_deadhead_km": round(deadhead_m / 1000, 3),
        "service_len_km": round(sum(u["service_len_m"] for u in order) / 1000, 3),
        "route": [
            {"unit_id": u["unit_id"], "name": u["name"], "start": u["start"], "end": u["end"]}
            for u in order
        ],
    }


def current_groups(units):
    groups = {}
    for u in units:
        groups.setdefault(u["current_vehicle_id"], []).append(u)
    return groups


def kmeans_groups(units, k):
    coords = np.array([[(u["start"][0] + u["end"][0]) / 2,
                        (u["start"][1] + u["end"][1]) / 2] for u in units])
    _, labels = kmeans2(coords, k, minit="++", seed=42)
    groups = {}
    for u, label in zip(units, labels):
        groups.setdefault(int(label), []).append(u)
    return groups


def total_deadhead(routes):
    return round(sum(r["inter_stop_deadhead_km"] for r in routes), 1)


def main():
    units = json.load(open(UNITS_FILE, encoding="utf-8"))
    today = [u for u in units if DAY_INDEX in u["operation_day_indexes"]]

    before = current_groups(today)
    truck_count = len(before)
    after = kmeans_groups(today, min(truck_count, len(today)))

    before_routes = [route_obj(str(vid), g) for vid, g in before.items()]
    after_routes = [route_obj(f"cluster-{i}", g) for i, g in after.items()]

    before_dead = total_deadhead(before_routes)
    after_dead = total_deadhead(after_routes)

    result = {
        "day_index": DAY_INDEX,
        "day_name": DAY_NAME,
        "method": "same truck count; before = current_vehicle_id, after = k-means geography; both NN-sequenced",
        "caveat": "geography/balance only; no time-window or shift constraint yet — optimistic bound",
        "summary": {
            "units": len(today),
            "trucks": truck_count,
            "before_deadhead_km": before_dead,
            "after_deadhead_km": after_dead,
            "reduction_pct": round(100 * (before_dead - after_dead) / before_dead, 1),
        },
        "before_routes": before_routes,
        "after_routes": after_routes,
    }
    json.dump(result, open(OUTPUT_FILE, "w", encoding="utf-8"), ensure_ascii=False, indent=2)
    print(json.dumps(result["summary"], ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
