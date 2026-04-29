package priority

import (
	"testing"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

func TestSortNodes(t *testing.T) {
	tests := []struct {
		name      string
		input     []model.Node
		wantOrder []string // expected node IDs in order
	}{
		{
			name: "sort by deadline ascending",
			input: []model.Node{
				{ID: "tomorrow", DeadlineMin: 2460, TWEnd: 1020, Priority: model.PriorityMedium},
				{ID: "today", DeadlineMin: 1020, TWEnd: 1020, Priority: model.PriorityMedium},
			},
			wantOrder: []string{"today", "tomorrow"},
		},
		{
			name: "no deadline sorts last",
			input: []model.Node{
				{ID: "no-deadline", DeadlineMin: 0, TWEnd: 900, Priority: model.PriorityCritical},
				{ID: "has-deadline", DeadlineMin: 1020, TWEnd: 900, Priority: model.PriorityLow},
			},
			wantOrder: []string{"has-deadline", "no-deadline"},
		},
		{
			name: "same deadline: sort by TWEnd ascending",
			input: []model.Node{
				{ID: "closes-late", DeadlineMin: 1020, TWEnd: 1200, Priority: model.PriorityHigh},
				{ID: "closes-early", DeadlineMin: 1020, TWEnd: 900, Priority: model.PriorityHigh},
			},
			wantOrder: []string{"closes-early", "closes-late"},
		},
		{
			name: "same deadline + TWEnd: sort by priority descending",
			input: []model.Node{
				{ID: "low", DeadlineMin: 1020, TWEnd: 1020, Priority: model.PriorityLow},
				{ID: "critical", DeadlineMin: 1020, TWEnd: 1020, Priority: model.PriorityCritical},
				{ID: "medium", DeadlineMin: 1020, TWEnd: 1020, Priority: model.PriorityMedium},
				{ID: "high", DeadlineMin: 1020, TWEnd: 1020, Priority: model.PriorityHigh},
			},
			wantOrder: []string{"critical", "high", "medium", "low"},
		},
		{
			name: "full mixed scenario",
			// realistic: plan for today (midnight=0)
			// today 17:00 = 1020, tomorrow 17:00 = 2460
			input: []model.Node{
				{ID: "D", DeadlineMin: 0, TWEnd: 900, Priority: model.PriorityLow},
				{ID: "C", DeadlineMin: 2460, TWEnd: 1020, Priority: model.PriorityMedium},
				{ID: "B", DeadlineMin: 1020, TWEnd: 1200, Priority: model.PriorityHigh},
				{ID: "A", DeadlineMin: 1020, TWEnd: 900, Priority: model.PriorityCritical},
			},
			wantOrder: []string{"A", "B", "C", "D"},
		},
		{
			name: "all same: stable order preserved",
			input: []model.Node{
				{ID: "X", DeadlineMin: 1020, TWEnd: 900, Priority: model.PriorityMedium},
				{ID: "Y", DeadlineMin: 1020, TWEnd: 900, Priority: model.PriorityMedium},
			},
			wantOrder: []string{"X", "Y"},
		},
		{
			name: "multiple no-deadline nodes: sorted by TWEnd then priority among themselves",
			input: []model.Node{
				{ID: "no-dl-low", DeadlineMin: 0, TWEnd: 900, Priority: model.PriorityLow},
				{ID: "no-dl-critical", DeadlineMin: 0, TWEnd: 900, Priority: model.PriorityCritical},
				{ID: "has-deadline", DeadlineMin: 1020, TWEnd: 1200, Priority: model.PriorityLow},
			},
			wantOrder: []string{"has-deadline", "no-dl-critical", "no-dl-low"},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			nodes := make([]model.Node, len(tt.input))
			copy(nodes, tt.input)

			SortNodes(nodes)

			gotOrder := make([]string, len(nodes))
			for i, n := range nodes {
				gotOrder[i] = n.ID
			}
			t.Logf("sorted order: %v", gotOrder)

			for i, wantID := range tt.wantOrder {
				if nodes[i].ID != wantID {
					t.Errorf("position %d: got %q, want %q", i, nodes[i].ID, wantID)
				}
			}
		})
	}
}

func TestSortNodesScattered(t *testing.T) {
	// กระจายครบทุกมิติ: deadline หลายวัน, TWEnd หลายช่วง, priority ทุกระดับ, มีทั้ง no-deadline
	nodes := []model.Node{
		{ID: "no-dl-low", DeadlineMin: 0, TWEnd: 600, Priority: model.PriorityLow},
		{ID: "no-dl-critical", DeadlineMin: 0, TWEnd: 600, Priority: model.PriorityCritical},
		{ID: "no-dl-medium-late", DeadlineMin: 0, TWEnd: 1200, Priority: model.PriorityMedium},
		{ID: "day3-high", DeadlineMin: 4320, TWEnd: 900, Priority: model.PriorityHigh},
		{ID: "day2-low", DeadlineMin: 2880, TWEnd: 720, Priority: model.PriorityLow},
		{ID: "day2-critical", DeadlineMin: 2880, TWEnd: 720, Priority: model.PriorityCritical},
		{ID: "tomorrow-medium", DeadlineMin: 2460, TWEnd: 1020, Priority: model.PriorityMedium},
		{ID: "tomorrow-high", DeadlineMin: 2460, TWEnd: 1020, Priority: model.PriorityHigh},
		{ID: "today-low-late", DeadlineMin: 1020, TWEnd: 1440, Priority: model.PriorityLow},
		{ID: "today-critical-early", DeadlineMin: 1020, TWEnd: 900, Priority: model.PriorityCritical},
		{ID: "today-high-early", DeadlineMin: 1020, TWEnd: 900, Priority: model.PriorityHigh},
		{ID: "today-medium-early", DeadlineMin: 1020, TWEnd: 900, Priority: model.PriorityMedium},
		{ID: "today-low-early", DeadlineMin: 1020, TWEnd: 900, Priority: model.PriorityLow},
		{ID: "today-high-mid", DeadlineMin: 1020, TWEnd: 1080, Priority: model.PriorityHigh},
		{ID: "no-priority", DeadlineMin: 540, TWEnd: 600, Priority: ""},
	}

	SortNodes(nodes)

	t.Log("=== sorted result ===")
	for i, n := range nodes {
		t.Logf("[%2d] {ID: %q, DeadlineMin: %d, TWEnd: %d, Priority: %q}",
			i+1, n.ID, n.DeadlineMin, n.TWEnd, n.Priority)
	}
}

func TestDeadlineKey(t *testing.T) {
	tests := []struct {
		name string
		node model.Node
		want int
	}{
		{"no deadline returns MaxInt32", model.Node{DeadlineMin: 0}, 1<<31 - 1},
		{"positive deadline returned as-is", model.Node{DeadlineMin: 1020}, 1020},
		{"tomorrow deadline", model.Node{DeadlineMin: 2460}, 2460},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := deadlineKey(tt.node); got != tt.want {
				t.Errorf("deadlineKey() = %d, want %d", got, tt.want)
			}
		})
	}
}

func TestPriorityRank(t *testing.T) {
	tests := []struct {
		priority model.Priority
		want     int
	}{
		{model.PriorityCritical, 4},
		{model.PriorityHigh, 3},
		{model.PriorityMedium, 2},
		{model.PriorityLow, 1},
		{"", 0},
	}

	for _, tt := range tests {
		t.Run(string(tt.priority), func(t *testing.T) {
			if got := priorityRank(tt.priority); got != tt.want {
				t.Errorf("priorityRank(%q) = %d, want %d", tt.priority, got, tt.want)
			}
		})
	}
}
