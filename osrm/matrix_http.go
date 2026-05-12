package osrm

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strconv"
	"strings"

	"github.com/ROP-TEAM/rop-algorithm/model"
)

func (m *OSRMMatrix) executeHTTP(ctx context.Context, url string) (*http.Response, error) {
	client := m.httpClient
	if client == nil {
		client = http.DefaultClient
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return nil, err
	}
	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("osrm request: %w", err)
	}
	return resp, nil
}

func readAndCheckHTTP(resp *http.Response) ([]byte, error) {
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("osrm returned HTTP %s", resp.Status)
	}
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("osrm read body: %w", err)
	}
	return body, nil
}

func parseTableResponse(body []byte, n int) ([][]int, [][]int, error) {
	return parseTableResponseWithShape(body, n, n)
}

func parseTableResponseWithShape(body []byte, rows, cols int) ([][]int, [][]int, error) {
	var table OSRMTableResponse
	if err := json.Unmarshal(body, &table); err != nil {
		return nil, nil, fmt.Errorf("osrm decode: %w", err)
	}

	if table.Code != "Ok" {
		msg := table.Message
		if msg == "" {
			msg = table.Code
		}
		return nil, nil, fmt.Errorf("osrm error: %s", msg)
	}

	if len(table.Durations) != rows {
		return nil, nil, fmt.Errorf("osrm returned %d rows, expected %d", len(table.Durations), rows)
	}

	durations := make([][]int, rows)
	distances := make([][]int, rows)
	for i := range rows {
		durations[i] = make([]int, cols)
		distances[i] = make([]int, cols)
		for j := range cols {
			durations[i][j] = int(table.Durations[i][j] / 60)
			distances[i][j] = int(table.Distances[i][j])
		}
	}

	return durations, distances, nil
}

func formatCoordinate(loc model.Location) string {
	return fmt.Sprintf("%s,%s",
		strconv.FormatFloat(loc.Lng, 'f', 6, 64),
		strconv.FormatFloat(loc.Lat, 'f', 6, 64),
	)
}

func formatCoordinates(locs []model.Location) string {
	parts := make([]string, len(locs))
	for i, loc := range locs {
		parts[i] = formatCoordinate(loc)
	}
	return strings.Join(parts, ";")
}

func indexRange(start, end int) string {
	parts := make([]string, end-start)
	for i := range parts {
		parts[i] = strconv.Itoa(start + i)
	}
	return strings.Join(parts, "%3B")
}
