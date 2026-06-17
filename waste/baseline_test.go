package waste

import "testing"

func unit(id, vehicle string, day int, lat, lng, lenM float64) RouteUnit {
	return RouteUnit{
		UnitID:            id,
		CurrentVehicleID:  vehicle,
		OperationDayIndex: []int{day},
		ServiceLenM:       lenM,
		Start:             LatLng{lat, lng},
		End:               LatLng{lat, lng},
	}
}

func TestBaselineDayGroupsByVehicle(t *testing.T) {
	units := []RouteUnit{
		unit("R1", "V1", 1, 13.70, 100.50, 1000),
		unit("R2", "V1", 1, 13.71, 100.50, 1000),
		unit("R3", "V2", 1, 13.80, 100.60, 1000),
		unit("R4", "V1", 2, 13.72, 100.50, 1000), // different day, excluded
	}
	got := BaselineDay(units, 1, 480, DefaultConfig())

	if got.TruckGroups != 2 {
		t.Errorf("TruckGroups = %d, want 2", got.TruckGroups)
	}
	if got.AssignedUnits != 3 {
		t.Errorf("AssignedUnits = %d, want 3", got.AssignedUnits)
	}
	if got.ServiceM != 3000 {
		t.Errorf("ServiceM = %.0f, want 3000", got.ServiceM)
	}
}

func TestBaselineDayDeadheadIsPositiveBetweenStops(t *testing.T) {
	units := []RouteUnit{
		unit("R1", "V1", 0, 13.70, 100.50, 500),
		unit("R2", "V1", 0, 13.75, 100.55, 500),
	}
	got := BaselineDay(units, 0, 480, DefaultConfig())

	if got.InterStopDeadheadM <= 0 {
		t.Errorf("InterStopDeadheadM = %.1f, want > 0", got.InterStopDeadheadM)
	}
}

func TestNNDeadheadSingleStopIsZero(t *testing.T) {
	if d := nnDeadheadM([]LatLng{{13.7, 100.5}}); d != 0 {
		t.Errorf("nnDeadheadM(single) = %.1f, want 0", d)
	}
}

func TestNNDeadheadPrefersNearChain(t *testing.T) {
	// A line of points: NN from the centre should walk the chain, never jumping
	// the full span twice.
	points := []LatLng{
		{13.70, 100.50},
		{13.71, 100.50},
		{13.72, 100.50},
		{13.73, 100.50},
	}
	got := nnDeadheadM(points)
	// Span end to end is one traversal; a sane tour is within ~2x the span.
	span := haversineM(points[0], points[len(points)-1])
	if got > 2*span {
		t.Errorf("nnDeadheadM = %.1f, want <= %.1f (2x span)", got, 2*span)
	}
}
