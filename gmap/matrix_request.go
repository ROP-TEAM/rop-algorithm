package gmap

import (
	"fmt"
	"net/url"
	"strconv"
	"strings"
)

func (g *GoogleMapsMatrix) buildDistanceMatrixURL(req DistanceMatrixRequest) string {
	baseURL := g.baseURL
	if baseURL == "" {
		baseURL = defaultDistanceMatrixURL
	}

	return baseURL + "?" + buildDistanceMatrixQuery(req, g.apiKey).Encode()
}

func buildDistanceMatrixQuery(req DistanceMatrixRequest, apiKey string) url.Values {
	query := url.Values{}
	query.Set("origins", strings.Join(req.Origins, "|"))
	query.Set("destinations", strings.Join(req.Destinations, "|"))
	query.Set("key", apiKey)

	mode := req.Mode
	if mode == "" {
		mode = "driving"
	}
	query.Set("mode", mode)

	units := req.Units
	if units == "" {
		units = "metric"
	}
	query.Set("units", units)

	if req.Language != "" {
		query.Set("language", req.Language)
	}
	if req.Region != "" {
		query.Set("region", req.Region)
	}
	if len(req.Avoid) > 0 {
		query.Set("avoid", strings.Join(req.Avoid, "|"))
	}
	if req.DepartureTimeNow {
		query.Set("departure_time", "now")
	} else if req.DepartureTime != 0 {
		query.Set("departure_time", strconv.FormatInt(req.DepartureTime, 10))
	}
	if req.ArrivalTime != 0 {
		query.Set("arrival_time", strconv.FormatInt(req.ArrivalTime, 10))
	}
	if req.TrafficModel != "" {
		query.Set("traffic_model", req.TrafficModel)
	}
	if len(req.TransitMode) > 0 {
		query.Set("transit_mode", strings.Join(req.TransitMode, "|"))
	}
	if req.TransitRoutingPreference != "" {
		query.Set("transit_routing_preference", req.TransitRoutingPreference)
	}

	return query
}

// BuildDistanceMatrixQuery is the public export of buildDistanceMatrixQuery.
func BuildDistanceMatrixQuery(req DistanceMatrixRequest, apiKey string) url.Values {
	return buildDistanceMatrixQuery(req, apiKey)
}

func buildDistanceMatrixRequest(locs []Location, opts MatrixOptions) DistanceMatrixRequest {
	points := make([]string, len(locs))
	for i, loc := range locs {
		points[i] = locationRequestValue(loc)
	}

	req := DistanceMatrixRequest{
		Origins:                  points,
		Destinations:             points,
		Mode:                     "driving",
		Units:                    "metric",
		Language:                 opts.Language,
		Region:                   opts.Region,
		Avoid:                    append([]string(nil), opts.Avoid...),
		DepartureTime:            opts.DepartureTime,
		DepartureTimeNow:         opts.DepartureTimeNow,
		ArrivalTime:              opts.ArrivalTime,
		TrafficModel:             opts.TrafficModel,
		TransitMode:              append([]string(nil), opts.TransitMode...),
		TransitRoutingPreference: opts.TransitRoutingPreference,
	}
	if opts.Mode != "" {
		req.Mode = opts.Mode
	}
	if opts.Units != "" {
		req.Units = opts.Units
	}

	return req
}

func validateDistanceMatrixRequest(req DistanceMatrixRequest) error {
	if len(req.Origins) == 0 {
		return fmt.Errorf("origins are required")
	}
	if len(req.Destinations) == 0 {
		return fmt.Errorf("destinations are required")
	}
	if req.DepartureTime != 0 && req.DepartureTimeNow {
		return fmt.Errorf("departure_time and departure_time=now cannot be used together")
	}
	if (req.DepartureTime != 0 || req.DepartureTimeNow) && req.ArrivalTime != 0 {
		return fmt.Errorf("departure_time and arrival_time cannot be used together")
	}

	return nil
}

// ValidateDistanceMatrixRequest is the public export of validateDistanceMatrixRequest.
func ValidateDistanceMatrixRequest(req DistanceMatrixRequest) error {
	return validateDistanceMatrixRequest(req)
}

func locationRequestValue(l Location) string {
	if strings.TrimSpace(l.Raw) != "" {
		return l.Raw
	}

	return fmt.Sprintf("%s,%s",
		strconv.FormatFloat(l.Lat, 'f', 6, 64),
		strconv.FormatFloat(l.Lng, 'f', 6, 64),
	)
}
