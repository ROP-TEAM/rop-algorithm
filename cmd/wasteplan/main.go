// Command wasteplan plans a full week of garbage collection from cleaned route
// units and prints metrics next to the current-operation baseline.
//
// The solver engine is selected by SOLVER_BINARY_PATH: when set it launches the
// C++ ALNS subprocess over gRPC, otherwise it falls back to the stub (which only
// reports that no engine is wired). Speeds are assumptions — sweep them.
package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"

	"github.com/ROP-TEAM/rop-algorithm/solver"
	grpcsolver "github.com/ROP-TEAM/rop-algorithm/solver/grpc"
	"github.com/ROP-TEAM/rop-algorithm/solver/process"
	"github.com/ROP-TEAM/rop-algorithm/waste"
)

// baseline holds the current-operation numbers from the Python analysis for
// side-by-side comparison (see clean_trash/REPORT.md).
var baseline = map[string]any{
	"inter_route_deadhead_km": 31177.3,
	"load_gini":               0.532,
	"vehicle_days":            11922,
	"note":                    "vehicle_days counts admin codes, not physical trucks",
}

func main() {
	opts := parseFlags()
	units, err := waste.LoadUnits(opts.unitsPath)
	if err != nil {
		fail(err)
	}

	engine, cleanup, err := selectSolver()
	if err != nil {
		fail(err)
	}
	defer cleanup()

	planner := waste.Planner{
		Solver:     engine,
		Config:     waste.Config{CollectKmh: opts.collectKmh, DriveKmh: opts.driveKmh, TimeLimitMS: opts.timeLimit, NoDepot: opts.noDepot},
		Fleet:       waste.Fleet{Mode: opts.mode, Count: opts.count, ShiftStart: opts.shiftStart, ShiftEnd: opts.shiftEnd, FixedCost: opts.fixedCost},
		MaxPerCell:  opts.maxPerCell,
		MaxShiftMin: opts.maxShiftMin,
	}

	report, err := runWeek(planner, units, opts)
	if err != nil {
		fail(err)
	}
	writeReport(report, opts.outPath)
}

func runWeek(planner waste.Planner, units []waste.RouteUnit, opts options) (map[string]any, error) {
	weekdays, err := parseDays(opts.days)
	if err != nil {
		return nil, err
	}
	report, err := planner.PlanDays(context.Background(), units, weekdays)
	if err != nil {
		return nil, err
	}
	t := report.Totals
	return map[string]any{
		"config":             map[string]any{"collect_kmh": opts.collectKmh, "drive_kmh": opts.driveKmh, "mode": opts.modeName, "max_per_cell": opts.maxPerCell},
		"baseline_reference": baseline,
		"baseline_native": map[string]any{
			"inter_stop_deadhead_km": round1(report.Baseline.InterStopDeadheadM / 1000),
			"assigned_units":         report.Baseline.AssignedUnits,
			"truck_groups":           report.Baseline.TruckGroups,
			"note":                   "current current_vehicle_id grouping, NN-ordered; same method as the plan",
		},
		"per_day": report.PerDay,
		"totals": map[string]any{
			"truck_shifts":                        t.TruckShifts,
			"deadhead_km_with_depot":              round1(t.DeadheadM / 1000),
			"inter_stop_deadhead_km":              round1(t.InterStopDeadheadM / 1000),
			"assigned_units":                      t.AssignedUnits,
			"unassigned":                          t.UnassignedUnits,
			"inter_stop_deadhead_vs_baseline_pct": t.InterStopReductionPct,
		},
	}, nil
}

func selectSolver() (solver.Solver, func(), error) {
	binary := os.Getenv("SOLVER_BINARY_PATH")
	if binary == "" {
		return solver.NewStub(), func() {}, nil
	}
	handle, err := process.Start(binary)
	if err != nil {
		return nil, nil, fmt.Errorf("start solver subprocess: %w", err)
	}
	return grpcsolver.New(handle.Conn), handle.Stop, nil
}

func writeReport(report map[string]any, outPath string) {
	encoded, err := json.MarshalIndent(report, "", "  ")
	if err != nil {
		fail(err)
	}
	if outPath == "" {
		fmt.Println(string(encoded))
		return
	}
	if err := os.WriteFile(outPath, encoded, 0o644); err != nil {
		fail(fmt.Errorf("write report %q: %w", outPath, err))
	}
	fmt.Printf("wrote %s\n", outPath)
}

func round1(v float64) float64 { return float64(int(v*10+0.5)) / 10 }

func fail(err error) {
	fmt.Fprintln(os.Stderr, "wasteplan:", err)
	os.Exit(1)
}
