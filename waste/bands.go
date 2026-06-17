package waste

import "sort"

// DefaultMaxShiftMin caps a merged band's span at 10 hours, matching the field
// shift observed in interviews (roughly 02:00–12:00). Overlapping windows are
// merged only while the union stays within this, so trucks keep a realistic day.
const DefaultMaxShiftMin = 600

// ShiftBand is a set of units a single truck-shift may serve, with the working
// window the truck operates in. Each unit is still collected inside its own time
// window; the band window is the horizon the solver schedules within.
type ShiftBand struct {
	Shift TimeWindow
	Units []RouteUnit
}

// MergeWindows groups units into shift bands by merging overlapping time windows,
// stopping a merge when the union would exceed maxShiftMin. This collapses the
// hundreds of exact-window fragments into a handful of realistic shifts, which
// is what cut deadhead by ~75% in the Python prototype.
func MergeWindows(units []RouteUnit, maxShiftMin int) []ShiftBand {
	if maxShiftMin <= 0 {
		maxShiftMin = DefaultMaxShiftMin
	}
	byWindow := make(map[TimeWindow][]RouteUnit)
	for _, u := range units {
		byWindow[u.TimeWindow] = append(byWindow[u.TimeWindow], u)
	}
	windows := sortedWindows(byWindow)

	var bands []ShiftBand
	for i := 0; i < len(windows); {
		start, end := windows[i].StartMin, windows[i].EndMin
		merged := append([]RouteUnit{}, byWindow[windows[i]]...)
		j := i + 1
		for j < len(windows) {
			w := windows[j]
			if w.StartMin >= end || maxInt(end, w.EndMin)-start > maxShiftMin {
				break
			}
			end = maxInt(end, w.EndMin)
			merged = append(merged, byWindow[w]...)
			j++
		}
		bands = append(bands, ShiftBand{Shift: TimeWindow{StartMin: start, EndMin: end}, Units: merged})
		i = j
	}
	return bands
}

func sortedWindows(byWindow map[TimeWindow][]RouteUnit) []TimeWindow {
	windows := make([]TimeWindow, 0, len(byWindow))
	for w := range byWindow {
		windows = append(windows, w)
	}
	sort.Slice(windows, func(i, j int) bool {
		if windows[i].StartMin != windows[j].StartMin {
			return windows[i].StartMin < windows[j].StartMin
		}
		return windows[i].EndMin < windows[j].EndMin
	})
	return windows
}

func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}
