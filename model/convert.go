package model

import (
	"fmt"
	"strconv"
	"strings"
)

// ToVehicle converts an InputVehicle to the internal Vehicle model.
func (v InputVehicle) ToVehicle() (Vehicle, error) {
	shiftStart, err := parseHHMM(v.WorkTime.Start)
	if err != nil {
		return Vehicle{}, fmt.Errorf("vehicle %d workTime.start: %w", v.ID, err)
	}
	shiftEnd, err := parseHHMM(v.WorkTime.End)
	if err != nil {
		return Vehicle{}, fmt.Errorf("vehicle %d workTime.end: %w", v.ID, err)
	}

	var breakStart, breakEnd int
	if v.BreakTime != nil {
		if breakStart, err = parseHHMM(v.BreakTime.Start); err != nil {
			return Vehicle{}, fmt.Errorf("vehicle %d breakTime.start: %w", v.ID, err)
		}
		if breakEnd, err = parseHHMM(v.BreakTime.End); err != nil {
			return Vehicle{}, fmt.Errorf("vehicle %d breakTime.end: %w", v.ID, err)
		}
	}

	return Vehicle{
		ID:         strconv.Itoa(v.ID),
		Capacity:   v.Capacity,
		ShiftStart: shiftStart,
		ShiftEnd:   shiftEnd,
		BreakStart: breakStart,
		BreakEnd:   breakEnd,
		MaxTasks:   v.MaxTask,
		Tags:       append([]string(nil), v.Skills...),
		StartLat:   v.StartLocation.Lat,
		StartLng:   v.StartLocation.Lng,
		EndLat:     v.EndLocation.Lat,
		EndLng:     v.EndLocation.Lng,
	}, nil
}

// ToNode converts an InputOrder to the internal Node model.
func (o InputOrder) ToNode() (Node, error) {
	twStart, err := parseHHMM(o.TimeWindow.Start)
	if err != nil {
		return Node{}, fmt.Errorf("order %d timeWindow.start: %w", o.ID, err)
	}
	twEnd, err := parseHHMM(o.TimeWindow.End)
	if err != nil {
		return Node{}, fmt.Errorf("order %d timeWindow.end: %w", o.ID, err)
	}

	return Node{
		ID:          strconv.Itoa(o.ID),
		Lat:         o.Location.Lat,
		Lng:         o.Location.Lng,
		Demand:      o.Capacity,
		ServiceTime: o.ServiceTime,
		TWStart:     twStart,
		TWEnd:       twEnd,
		Tags:        append([]string(nil), o.Skills...),
		Type:        o.Type,
		Priority:    o.Priority,
	}, nil
}

// parseHHMM parses "HH:mm" to minutes from midnight.
func parseHHMM(s string) (int, error) {
	if s == "" {
		return 0, nil
	}
	hh, mm, ok := strings.Cut(s, ":")
	if !ok {
		return 0, fmt.Errorf("invalid time %q: expected HH:mm", s)
	}
	hours, err := strconv.Atoi(hh)
	if err != nil {
		return 0, fmt.Errorf("invalid time %q: %w", s, err)
	}
	minutes, err := strconv.Atoi(mm)
	if err != nil {
		return 0, fmt.Errorf("invalid time %q: %w", s, err)
	}
	if hours > 23 {
		return 0, fmt.Errorf("invalid time %q: hours must be 0-23", s)
	}
	if minutes > 59 {
		return 0, fmt.Errorf("invalid time %q: minutes must be 0-59", s)
	}
	return hours*60 + minutes, nil
}
