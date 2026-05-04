package gmap

import (
	"fmt"
	"net/http"
	"strings"
	"time"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

func WithBaseURL(baseURL string) GoogleMapsMatrixOption {
	return func(g *GoogleMapsMatrix) {
		g.baseURL = baseURL
	}
}

func WithHTTPClient(client *http.Client) GoogleMapsMatrixOption {
	return func(g *GoogleMapsMatrix) {
		g.httpClient = client
	}
}

func WithClock(now func() time.Time) GoogleMapsMatrixOption {
	return func(g *GoogleMapsMatrix) {
		g.now = now
	}
}

// WithInMemoryCache enables the built-in in-memory cache as a constructor option.
func WithInMemoryCache(cfg model.MatrixCacheConfig) GoogleMapsMatrixOption {
	return func(g *GoogleMapsMatrix) {
		g.cache = NewMemoryMatrixCache()
		g.cacheConfig = cfg
	}
}

func NewGoogleMapsMatrix(apiKey string, opts ...GoogleMapsMatrixOption) (*GoogleMapsMatrix, error) {
	if strings.TrimSpace(apiKey) == "" {
		return nil, fmt.Errorf("google maps api key is required")
	}

	g := &GoogleMapsMatrix{
		apiKey:      apiKey,
		httpClient:  http.DefaultClient,
		baseURL:     defaultDistanceMatrixURL,
		cacheConfig: DefaultMatrixCacheConfig(),
		now:         time.Now,
	}
	for _, opt := range opts {
		if opt != nil {
			opt(g)
		}
	}

	return g, nil
}
