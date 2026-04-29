package timeWindow

import "testing"

func TestOverlaps(t *testing.T) {
	tests := []struct {
		name   string
		aStart int
		aEnd   int
		bStart int
		bEnd   int
		want   bool
	}{
		// overlapping
		{"full overlap", 540, 1020, 600, 900, true},
		{"partial overlap at start", 540, 900, 800, 1080, true},
		{"partial overlap at end", 800, 1080, 540, 900, true},
		{"b contains a", 600, 900, 540, 1020, true},
		{"a contains b", 540, 1020, 600, 900, true},
		{"same range", 540, 900, 540, 900, true},

		// not overlapping
		{"a ends where b starts (adjacent, no overlap)", 540, 900, 900, 1080, false},
		{"a before b with gap", 540, 720, 780, 1020, false},
		{"b before a with gap", 780, 1020, 540, 720, false},
		{"a entirely after b", 1020, 1200, 540, 900, false},

		// real-world vehicle shift vs order time window
		{"vehicle 08:00-17:00 vs order 09:00-12:00", 480, 1020, 540, 720, true},
		{"vehicle 08:00-12:00 vs order 13:00-17:00 (no overlap)", 480, 720, 780, 1020, false},
		{"vehicle 14:00-20:00 vs order 09:00-14:00 (adjacent)", 840, 1200, 540, 840, false},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := Overlaps(tt.aStart, tt.aEnd, tt.bStart, tt.bEnd)
			if got != tt.want {
				t.Errorf("Overlaps(%d,%d,%d,%d) = %v, want %v",
					tt.aStart, tt.aEnd, tt.bStart, tt.bEnd, got, tt.want)
			}
		})
	}
}
