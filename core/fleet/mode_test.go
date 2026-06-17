package fleet

import "testing"

func TestFixedCost(t *testing.T) {
	tests := []struct {
		name string
		mode Mode
		want float64
	}{
		{"minimize charges the dominating penalty", Minimize, FixedCostPenalty},
		{"full fleet is free per vehicle", Full, 0},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := FixedCost(tt.mode); got != tt.want {
				t.Errorf("FixedCost(%v) = %.0f, want %.0f", tt.mode, got, tt.want)
			}
		})
	}
}
