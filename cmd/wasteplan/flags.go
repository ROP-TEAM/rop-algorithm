package main

import (
	"flag"
	"fmt"
	"strconv"
	"strings"

	"github.com/ROP-TEAM/rop-algorithm/waste"
)

type options struct {
	unitsPath  string
	outPath    string
	collectKmh float64
	driveKmh   float64
	shiftStart int
	shiftEnd   int
	maxPerCell  int
	maxShiftMin int
	count       int
	timeLimit   int
	noDepot     bool
	fixedCost   float64
	days        string
	mode        waste.FleetMode
	modeName    string
}

func parseFlags() options {
	unitsPath := flag.String("units", "../clean_trash/7_route_units.json", "path to route units JSON")
	outPath := flag.String("out", "", "output metrics JSON path (default stdout)")
	collect := flag.Float64("collect", 5.0, "collection speed km/h (assumption)")
	drive := flag.Float64("drive", 20.0, "driving speed km/h (assumption)")
	shiftStart := flag.Int("shift-start", 0, "shift start, minutes from midnight")
	shiftEnd := flag.Int("shift-end", 480, "shift end, minutes from midnight")
	maxPerCell := flag.Int("max-per-cell", 300, "max route units per subproblem")
	maxShiftMin := flag.Int("max-shift-min", waste.DefaultMaxShiftMin, "cap on a merged shift band (minutes)")
	mode := flag.String("mode", "min", "fleet mode: min (fewest trucks) or full (fixed count)")
	count := flag.Int("count", 0, "truck count per cell for full mode")
	timeLimit := flag.Int("time-limit-ms", 300, "solver budget per subproblem (ms)")
	noDepot := flag.Bool("no-depot", false, "zero the depot in the matrix (unsafe: solver teleports — keep off)")
	fixedCost := flag.Float64("fixed-cost", 0, "per-truck fixed cost override for min mode; 0 = strategy default (100000)")
	days := flag.String("days", "", "comma-separated weekdays to plan (0=Sun); empty = all 7 (use one day for a fast sweep)")
	flag.Parse()

	fleetMode := waste.MinimizeFleet
	if *mode == "full" {
		fleetMode = waste.FullFleet
	}
	return options{
		unitsPath:  *unitsPath,
		outPath:    *outPath,
		collectKmh: *collect,
		driveKmh:   *drive,
		shiftStart: *shiftStart,
		shiftEnd:   *shiftEnd,
		maxPerCell:  *maxPerCell,
		maxShiftMin: *maxShiftMin,
		count:       *count,
		timeLimit:   *timeLimit,
		noDepot:     *noDepot,
		fixedCost:   *fixedCost,
		days:        *days,
		mode:        fleetMode,
		modeName:    *mode,
	}
}

// parseDays turns a comma-separated weekday list ("1,4") into indexes; an empty
// string means the full week. Used to run a single day for a fast sweep.
func parseDays(spec string) ([]int, error) {
	if strings.TrimSpace(spec) == "" {
		return waste.AllWeekdays, nil
	}
	var days []int
	for _, part := range strings.Split(spec, ",") {
		d, err := strconv.Atoi(strings.TrimSpace(part))
		if err != nil || d < 0 || d > 6 {
			return nil, fmt.Errorf("invalid weekday %q: must be 0-6", part)
		}
		days = append(days, d)
	}
	return days, nil
}
