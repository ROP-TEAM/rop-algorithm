package gmap

import (
	"context"
	"strconv"
	"strings"
	"sync"
)

// MatrixEventHook is a lightweight logging hook for cache and API events.
type MatrixEventHook func(ctx context.Context, event MatrixEvent)

// MatrixMetricsCollector records matrix events into a metrics backend.
type MatrixMetricsCollector interface {
	RecordMatrixEvent(event MatrixEvent)
}

// MatrixMetrics is a simple in-memory counter collector for matrix events.
type MatrixMetrics struct {
	mu       sync.Mutex
	counters map[string]int64
}

func NewMatrixMetrics() *MatrixMetrics {
	return &MatrixMetrics{
		counters: make(map[string]int64),
	}
}

func (m *MatrixMetrics) RecordMatrixEvent(event MatrixEvent) {
	if m == nil {
		return
	}

	key := matrixMetricKey(event)

	m.mu.Lock()
	m.counters[key]++
	m.mu.Unlock()
}

func (m *MatrixMetrics) Snapshot() map[string]int64 {
	if m == nil {
		return nil
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	out := make(map[string]int64, len(m.counters))
	for k, v := range m.counters {
		out[k] = v
	}
	return out
}

func matrixMetricKey(event MatrixEvent) string {
	parts := []string{event.Name}
	if event.Policy != "" {
		parts = append(parts, "policy="+string(event.Policy))
	}
	if event.Reason != "" {
		parts = append(parts, "reason="+event.Reason)
	}
	if event.Error != "" {
		parts = append(parts, "error="+event.Error)
	}
	if event.ChunkOrigins > 0 {
		parts = append(parts, "origins="+strconv.Itoa(event.ChunkOrigins))
	}
	if event.ChunkDestinations > 0 {
		parts = append(parts, "destinations="+strconv.Itoa(event.ChunkDestinations))
	}
	return strings.Join(parts, "|")
}
