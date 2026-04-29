package priority

import (
	"math"
	"sort"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

// SortNodes sorts in-place: earliest deadline first, then earliest close time,
// then highest priority. Nodes with no deadline (DeadlineMin == 0) sort last.
func SortNodes(nodes []model.Node) {
	sort.SliceStable(nodes, func(i, j int) bool {
		return isMoreUrgent(nodes[i], nodes[j])
	})
}

func isMoreUrgent(a, b model.Node) bool {
	da, db := deadlineKey(a), deadlineKey(b)
	if da != db {
		return da < db
	}
	if a.TWEnd != b.TWEnd {
		return a.TWEnd < b.TWEnd
	}
	return priorityRank(a.Priority) > priorityRank(b.Priority)
}

func deadlineKey(n model.Node) int {
	if n.DeadlineMin == 0 {
		return math.MaxInt32
	}
	return n.DeadlineMin
}

func priorityRank(p model.Priority) int {
	switch p {
	case model.PriorityCritical:
		return 4
	case model.PriorityHigh:
		return 3
	case model.PriorityMedium:
		return 2
	case model.PriorityLow:
		return 1
	default:
		return 0
	}
}
