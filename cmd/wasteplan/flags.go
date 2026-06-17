package main

import (
	"flag"

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
		mode:        fleetMode,
		modeName:    *mode,
	}
}
