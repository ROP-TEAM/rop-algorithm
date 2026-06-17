# Phase 1 re-grouping proof (route format)

`9_proto_routes_day3.json` shows the deadhead reduction claimed in
`clean_trash/REPORT.md` as **actual routes**, not just a single number.

## What it shows

One weekday (Wednesday, the busiest — 5,895 routes due) built into trucks two
ways, with identical nearest-neighbour sequencing on both sides:

- `before_routes` — grouped by the current administrative vehicle
  (`current_vehicle_id`)
- `after_routes` — grouped by geography (k-means) into the **same number of
  trucks**

Each truck is an ordered list of route units (`unit_id`, `name`, `start`, `end`)
with its own `inter_stop_deadhead_km`.

## Result

| | trucks | inter-stop deadhead |
|---|---|---|
| before (current) | 1,708 | 4,490.3 km |
| after (re-grouped) | 1,708 | 2,048.5 km |
| **change** | same | **−54.4 %** |

The truck count is held fixed, so the gain comes purely from *which routes ride
together* — the assignment lever, not better sequencing (the baseline is already
nearest-neighbour optimal for its grouping).

## Caveat

Geography and balance only; this proof does **not** enforce time windows or
shift length. Those are hard constraints in the real C++ solver, so the figure
is an optimistic upper bound on the assignment gain, not the final number.

## Regenerate

```bash
cd clean_trash
python proto_routes.py   # needs 7_route_units.json, numpy, scipy
```

`proto_routes.py` here is a copy of that generator for reference.
