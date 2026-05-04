package gmap

import (
	"context"
	"sync"
	"time"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

// MatrixCache is the storage contract for caching resolved matrix results.
type MatrixCache interface {
	Get(ctx context.Context, key string) (*model.DistanceMatrixResult, bool, error)
	Set(ctx context.Context, key string, value *model.DistanceMatrixResult, ttl time.Duration) error
}

type memoryMatrixCacheEntry struct {
	value     *model.DistanceMatrixResult
	expiresAt time.Time
}

// MemoryMatrixCache is a simple in-memory cache implementation intended for single-process use.
type MemoryMatrixCache struct {
	mu      sync.RWMutex
	entries map[string]memoryMatrixCacheEntry
	now     func() time.Time
}

func NewMemoryMatrixCache() *MemoryMatrixCache {
	return &MemoryMatrixCache{
		entries: make(map[string]memoryMatrixCacheEntry),
		now:     time.Now,
	}
}

func (c *MemoryMatrixCache) Get(ctx context.Context, key string) (*model.DistanceMatrixResult, bool, error) {
	_ = ctx

	nowFn := c.now
	if nowFn == nil {
		nowFn = time.Now
	}

	c.mu.RLock()
	entry, ok := c.entries[key]
	c.mu.RUnlock()
	if !ok {
		return nil, false, nil
	}

	if !entry.expiresAt.IsZero() && !entry.expiresAt.After(nowFn()) {
		c.mu.Lock()
		delete(c.entries, key)
		c.mu.Unlock()
		return nil, false, nil
	}

	return cloneDistanceMatrixResult(entry.value), true, nil
}

func (c *MemoryMatrixCache) Set(ctx context.Context, key string, value *model.DistanceMatrixResult, ttl time.Duration) error {
	_ = ctx

	nowFn := c.now
	if nowFn == nil {
		nowFn = time.Now
	}

	entry := memoryMatrixCacheEntry{
		value: cloneDistanceMatrixResult(value),
	}
	if ttl > 0 {
		entry.expiresAt = nowFn().Add(ttl)
	}

	c.mu.Lock()
	c.entries[key] = entry
	c.mu.Unlock()
	return nil
}

func cloneDistanceMatrixResult(src *model.DistanceMatrixResult) *model.DistanceMatrixResult {
	if src == nil {
		return nil
	}

	dst := &model.DistanceMatrixResult{
		Request:   cloneDistanceMatrixRequest(src.Request),
		Response:  cloneDistanceMatrixResponse(src.Response),
		Durations: cloneIntMatrix(src.Durations),
		Distances: cloneIntMatrix(src.Distances),
	}
	return dst
}

func cloneDistanceMatrixRequest(src model.DistanceMatrixRequest) model.DistanceMatrixRequest {
	return model.DistanceMatrixRequest{
		Origins:                  append([]string(nil), src.Origins...),
		Destinations:             append([]string(nil), src.Destinations...),
		Mode:                     src.Mode,
		Units:                    src.Units,
		Language:                 src.Language,
		Region:                   src.Region,
		Avoid:                    append([]string(nil), src.Avoid...),
		DepartureTime:            src.DepartureTime,
		DepartureTimeNow:         src.DepartureTimeNow,
		TrafficModel:             src.TrafficModel,
		ArrivalTime:              src.ArrivalTime,
		TransitMode:              append([]string(nil), src.TransitMode...),
		TransitRoutingPreference: src.TransitRoutingPreference,
	}
}

func cloneDistanceMatrixResponse(src model.DistanceMatrixResponse) model.DistanceMatrixResponse {
	dst := model.DistanceMatrixResponse{
		Status:               src.Status,
		ErrorMessage:         src.ErrorMessage,
		OriginAddresses:      append([]string(nil), src.OriginAddresses...),
		DestinationAddresses: append([]string(nil), src.DestinationAddresses...),
		Rows:                 make([]model.DistanceMatrixRow, len(src.Rows)),
	}

	for i, row := range src.Rows {
		dst.Rows[i] = model.DistanceMatrixRow{
			Elements: make([]model.DistanceMatrixElement, len(row.Elements)),
		}
		for j, el := range row.Elements {
			dst.Rows[i].Elements[j] = model.DistanceMatrixElement{
				Status:            el.Status,
				Distance:          cloneValueText(el.Distance),
				Duration:          cloneValueText(el.Duration),
				DurationInTraffic: cloneValueText(el.DurationInTraffic),
				Fare:              cloneTransitFare(el.Fare),
			}
		}
	}

	return dst
}

func cloneIntMatrix(src [][]int) [][]int {
	if src == nil {
		return nil
	}

	dst := make([][]int, len(src))
	for i := range src {
		dst[i] = append([]int(nil), src[i]...)
	}
	return dst
}

func cloneValueText(src *model.ValueText) *model.ValueText {
	if src == nil {
		return nil
	}

	dst := *src
	return &dst
}

func cloneTransitFare(src *model.TransitFare) *model.TransitFare {
	if src == nil {
		return nil
	}

	dst := *src
	return &dst
}
