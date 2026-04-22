package model

import "time"

// Travel mode values for MatrixOptions.Mode.
const (
	ModeDriving   = "driving"
	ModeWalking   = "walking"
	ModeBicycling = "bicycling"
	ModeTransit   = "transit"
)

// Traffic model values for MatrixOptions.TrafficModel.
const (
	TrafficModelBestGuess   = "best_guess"
	TrafficModelPessimistic = "pessimistic"
	TrafficModelOptimistic  = "optimistic"
)

// Route restriction values for MatrixOptions.Avoid.
const (
	AvoidTolls    = "tolls"
	AvoidHighways = "highways"
	AvoidFerries  = "ferries"
	AvoidIndoor   = "indoor"
)

// NewLatLngLocation creates a Location from a latitude/longitude pair.
func NewLatLngLocation(lat, lng float64) Location {
	return Location{Lat: lat, Lng: lng}
}

// NewRawLocation creates a Location from a pre-formatted address, place ID,
// plus code, or any other raw string the Distance Matrix API accepts directly.
func NewRawLocation(raw string) Location {
	return Location{Raw: raw}
}

// Location is a convenience type used by the compatibility BuildMatrix wrapper.
type Location struct {
	Lat float64
	Lng float64

	// Raw overrides Lat/Lng formatting and is sent as-is to the API.
	// Supports address, place_id:..., plus code, enc:...:, side_of_road:..., heading=X:...
	Raw string
}

// MatrixOptions configures optional parameters for BuildMatrix.
type MatrixOptions struct {
	// DepartureTime: unix timestamp; 0 = use normal duration (no traffic)
	// When set, returns duration_in_traffic (real road conditions at that time).
	// Must be current or future time only.
	DepartureTime int64

	// DepartureTimeNow sends departure_time=now.
	// This is useful for driving traffic or transit requests that should use current time.
	DepartureTimeNow bool

	// ArrivalTime: unix timestamp for transit requests.
	// Cannot be used together with DepartureTime or DepartureTimeNow.
	ArrivalTime int64

	// TrafficModel: "best_guess"|"pessimistic"|"optimistic" (requires DepartureTime)
	// Default "best_guess" if empty.
	TrafficModel string

	// Avoid: route restrictions — any of "tolls", "highways", "ferries", "indoor"
	Avoid []string

	// Mode: "driving"|"walking"|"bicycling"|"transit" — default "driving"
	Mode string

	// Units: "metric" | "imperial"
	Units string

	// Language: BCP-47 code e.g. "en", "th"
	Language string

	// Region: ccTLD e.g. "th", "us"
	Region string

	// Transit-only fields
	TransitMode              []string // "bus"|"subway"|"train"|"tram"|"rail"
	TransitRoutingPreference string   // "less_walking"|"fewer_transfers"
}

// SetDepartureTime sets DepartureTime from a time.Time value, clears DepartureTimeNow.
func (o *MatrixOptions) SetDepartureTime(t time.Time) {
	o.DepartureTime = t.Unix()
	o.DepartureTimeNow = false
}

// SetArrivalTime sets ArrivalTime from a time.Time value, clears DepartureTime and DepartureTimeNow.
func (o *MatrixOptions) SetArrivalTime(t time.Time) {
	o.ArrivalTime = t.Unix()
	o.DepartureTime = 0
	o.DepartureTimeNow = false
}

// DistanceMatrixRequest holds all parameters for a Distance Matrix API call.
type DistanceMatrixRequest struct {
	Origins      []string
	Destinations []string

	// Mode: "driving" | "walking" | "bicycling" | "transit" (default: "driving")
	Mode string

	// Units: "metric" | "imperial"
	Units string

	// Language: BCP-47 code e.g. "en", "th"
	Language string

	// Region: ccTLD e.g. "th", "us"
	Region string

	// Avoid: any of "tolls", "highways", "ferries", "indoor"
	Avoid []string

	// DepartureTime: unix timestamp or 0 to omit; enables DurationInTraffic in response
	DepartureTime int64

	// DepartureTimeNow sends departure_time=now.
	DepartureTimeNow bool

	// TrafficModel: "best_guess" | "pessimistic" | "optimistic" (requires DepartureTime)
	TrafficModel string

	// Transit-only fields
	ArrivalTime              int64
	TransitMode              []string // "bus"|"subway"|"train"|"tram"|"rail"
	TransitRoutingPreference string   // "less_walking"|"fewer_transfers"
}

// DistanceMatrixResult is the parsed output of a Distance Matrix API request.
type DistanceMatrixResult struct {
	Request   DistanceMatrixRequest  `json:"request"`
	Response  DistanceMatrixResponse `json:"response"`
	Durations [][]int                `json:"durations"`
	Distances [][]int                `json:"distances"`
}

// DistanceMatrixResponse is the full API response.
type DistanceMatrixResponse struct {
	// OK | INVALID_REQUEST | MAX_ELEMENTS_EXCEEDED | MAX_DIMENSIONS_EXCEEDED |
	// OVER_DAILY_LIMIT | OVER_QUERY_LIMIT | REQUEST_DENIED | UNKNOWN_ERROR
	Status               string              `json:"status"`
	ErrorMessage         string              `json:"error_message,omitempty"`
	OriginAddresses      []string            `json:"origin_addresses"`
	DestinationAddresses []string            `json:"destination_addresses"`
	Rows                 []DistanceMatrixRow `json:"rows"`
}

type DistanceMatrixRow struct {
	Elements []DistanceMatrixElement `json:"elements"`
}

// DistanceMatrixElement represents one origin→destination pair.
type DistanceMatrixElement struct {
	// OK | NOT_FOUND | ZERO_RESULTS | MAX_ROUTE_LENGTH_EXCEEDED
	// Distance/Duration are absent (nil) when status != OK
	Status string `json:"status"`

	// Distance in meters (value); human-readable text per units param
	Distance *ValueText `json:"distance,omitempty"`

	// Duration in seconds (value); human-readable text
	Duration *ValueText `json:"duration,omitempty"`

	// DurationInTraffic: only present for driving + departure_time
	DurationInTraffic *ValueText `json:"duration_in_traffic,omitempty"`

	// Fare: only present for transit mode when provider supports it
	Fare *TransitFare `json:"fare,omitempty"`
}

// ValueText is used for both distance (meters) and duration (seconds).
type ValueText struct {
	Value int    `json:"value"`
	Text  string `json:"text"`
}

// TransitFare holds fare information for transit routes.
type TransitFare struct {
	Value    float64 `json:"value"`
	Currency string  `json:"currency"` // ISO 4217 e.g. "THB", "USD"
	Text     string  `json:"text"`
}
