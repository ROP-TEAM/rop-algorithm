package model

// NodeType identifies the role of a stop in a route.
type NodeType string

const (
	NodeTypeDepot    NodeType = "depot"
	NodeTypeDelivery NodeType = "delivery"
	NodeTypePickup   NodeType = "pickup"
)

// Node is a stop in the routing problem.
type Node struct {
	ID          string
	Lat         float64
	Lng         float64
	Demand      int      // units consumed from vehicle capacity
	ServiceTime int      // minutes to spend at this stop
	TWStart     int      // earliest arrival, minutes from midnight
	TWEnd       int      // latest arrival, minutes from midnight
	Tags        []string // must match at least one vehicle tag
	Type        NodeType
	PairID      string // links pickup↔delivery; empty if not a PD pair
}
