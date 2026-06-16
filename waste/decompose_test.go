package waste

import "testing"

func gridUnits(n int) []RouteUnit {
	units := make([]RouteUnit, n)
	for i := range units {
		lat := 13.70 + float64(i)*0.001
		units[i] = RouteUnit{
			UnitID:            "R_" + string(rune('A'+i)),
			OperationDayIndex: []int{i % 7},
			Start:             LatLng{lat, 100.50},
			End:               LatLng{lat, 100.50},
		}
	}
	return units
}

func TestDecomposeGridRespectsMaxPerCell(t *testing.T) {
	cells := DecomposeGrid(gridUnits(10), 3)
	total := 0
	for _, cell := range cells {
		if len(cell) > 3 {
			t.Errorf("cell size %d exceeds max 3", len(cell))
		}
		total += len(cell)
	}
	if total != 10 {
		t.Errorf("partition dropped units: total %d, want 10", total)
	}
}

func TestDecomposeGridSinglePassWhenSmall(t *testing.T) {
	cells := DecomposeGrid(gridUnits(2), 5)
	if len(cells) != 1 {
		t.Errorf("expected 1 cell for small input, got %d", len(cells))
	}
}

func TestUnitsForDayFilters(t *testing.T) {
	units := []RouteUnit{
		{UnitID: "a", OperationDayIndex: []int{1, 3}},
		{UnitID: "b", OperationDayIndex: []int{2}},
	}
	got := UnitsForDay(units, 3)
	if len(got) != 1 || got[0].UnitID != "a" {
		t.Errorf("UnitsForDay(3) = %+v, want only a", got)
	}
}
