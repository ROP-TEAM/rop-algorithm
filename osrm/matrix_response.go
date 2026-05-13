package osrm

// OSRMTableResponse is the raw JSON response from GET /table/v1/{profile}/{coordinates}.
type OSRMTableResponse struct {
	Code    string `json:"code"` // "Ok" | "NoTable" | "InvalidValue"
	Message string `json:"message,omitempty"`

	Durations [][]float64 `json:"durations"`
	Distances [][]float64 `json:"distances"`
}

// OSRMNearestResponse is the raw JSON response from GET /nearest/v1/{profile}/{coordinate}.
type OSRMNearestResponse struct {
	Code      string         `json:"code"`
	Waypoints []OSRMWaypoint `json:"waypoints"`
}

// OSRMWaypoint is a snap point returned by /nearest.
type OSRMWaypoint struct {
	Name     string     `json:"name"`
	Location [2]float64 `json:"location"` // [lon, lat]
	Distance float64    `json:"distance"`
	Hint     string     `json:"hint"`
}

// NearestPoint is the public result of Nearest().
type NearestPoint struct {
	Name     string
	Lat      float64
	Lng      float64
	Distance float64
	Hint     string
}

// OSRMTripResponse is the raw JSON response from GET /trip/v1/{profile}/{coordinates}.
type OSRMTripResponse struct {
	Code      string            `json:"code"`
	Message   string            `json:"message,omitempty"`
	Waypoints []OSRMTripWaypoint `json:"waypoints"`
	Trips     []OSRMTripRoute   `json:"trips"`
}

// OSRMTripWaypoint is a waypoint in a trip response with visit-order indices.
type OSRMTripWaypoint struct {
	Name          string     `json:"name"`
	Location      [2]float64 `json:"location"` // [lon, lat]
	TripsIndex    int        `json:"trips_index"`
	WaypointIndex int        `json:"waypoint_index"`
	Hint          string     `json:"hint"`
}

// OSRMTripRoute is one trip (one vehicle route) in the TSP solution.
type OSRMTripRoute struct {
	Distance float64 `json:"distance"`
	Duration float64 `json:"duration"`
	Geometry string  `json:"geometry"`
}

// TripResult is the public result of Trip().
type TripResult struct {
	Waypoints []TripWaypoint
	Distance  float64
	Duration  float64
	Geometry  string
}

// TripWaypoint is a waypoint in the Trip result.
type TripWaypoint struct {
	Name          string
	Lat           float64
	Lng           float64
	TripsIndex    int
	WaypointIndex int
	Hint          string
}

// OSRMRouteResponse is the raw JSON response from GET /route/v1/{profile}/{coordinates}.
type OSRMRouteResponse struct {
	Code      string         `json:"code"`
	Message   string         `json:"message,omitempty"`
	Waypoints []OSRMWaypoint `json:"waypoints"`
	Routes    []OSRMRoute    `json:"routes"`
}

// OSRMRoute is a single route alternative from /route.
type OSRMRoute struct {
	Distance float64 `json:"distance"`
	Duration float64 `json:"duration"`
	Geometry string  `json:"geometry"`
}

// RouteResult is the public result of Route().
type RouteResult struct {
	Waypoints []RouteWaypoint
	Routes    []RouteItem
}

// RouteWaypoint is a snapped waypoint from the route response.
type RouteWaypoint struct {
	Name string
	Lat  float64
	Lng  float64
}

// RouteItem is one route alternative.
type RouteItem struct {
	Distance float64
	Duration float64
	Geometry string
}
