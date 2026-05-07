package model

// NodeType identifies the role of a stop in a route.
type NodeType string

const (
	NodeTypeDepot    NodeType = "depot"
	NodeTypeDelivery NodeType = "delivery"
	NodeTypePickup   NodeType = "pickup"
)

// Priority is the urgency level of a delivery order.
type Priority string

const (
	PriorityCritical Priority = "critical"
	PriorityHigh     Priority = "high"
	PriorityMedium   Priority = "medium"
	PriorityLow      Priority = "low"
)

// Rank returns the numeric priority used for sorting and proto encoding.
// critical=4 > high=3 > medium=2 > low=1 > ""=0.
func (p Priority) Rank() int32 {
	switch p {
	case PriorityCritical:
		return 4
	case PriorityHigh:
		return 3
	case PriorityMedium:
		return 2
	case PriorityLow:
		return 1
	default:
		return 0
	}
}

// Node is a stop in the routing problem.
type Node struct {
	ID          string
	Lat         float64
	Lng         float64
	Demand      int      // legacy capacity demand; used when linehaul/backhaul are unset
	Linehaul    int      // delivery load consumed from depot
	Backhaul    int      // pickup load added during route
	ServiceTime int      // minutes to spend at this stop
	TWStart     int      // earliest arrival, minutes from midnight
	TWEnd       int      // latest arrival, minutes from midnight
	Tags        []string // must match at least one vehicle tag
	Type        NodeType
	PairID      string // links pickup↔delivery; empty if not a PD pair
	Priority    Priority
	DeadlineMin int // minutes from planning midnight; 0 = no deadline
	MustServe   bool
}
