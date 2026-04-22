package gmap

import "fmt"

type matrixChunk struct {
	req    DistanceMatrixRequest
	oStart int
	dStart int
}

func buildChunk(req DistanceMatrixRequest, oStart, oEnd, dStart, dEnd int) matrixChunk {
	chunk := matrixChunk{req: req, oStart: oStart, dStart: dStart}
	chunk.req.Origins = append([]string(nil), req.Origins[oStart:oEnd]...)
	chunk.req.Destinations = append([]string(nil), req.Destinations[dStart:dEnd]...)
	return chunk
}

func allocateResult(req DistanceMatrixRequest) *DistanceMatrixResult {
	nO, nD := len(req.Origins), len(req.Destinations)
	rows := make([]DistanceMatrixRow, nO)
	for i := range rows {
		rows[i] = DistanceMatrixRow{Elements: make([]DistanceMatrixElement, nD)}
	}
	return &DistanceMatrixResult{
		Request: req,
		Response: DistanceMatrixResponse{
			Status:               "OK",
			OriginAddresses:      make([]string, nO),
			DestinationAddresses: make([]string, nD),
			Rows:                 rows,
		},
		Durations: makeMatrix(nO, nD),
		Distances: makeMatrix(nO, nD),
	}
}

func mergeChunkIntoResult(result *DistanceMatrixResult, chunk matrixChunk, resp DistanceMatrixResponse) error {
	copy(result.Response.OriginAddresses[chunk.oStart:], resp.OriginAddresses)
	copy(result.Response.DestinationAddresses[chunk.dStart:], resp.DestinationAddresses)
	for ri, row := range resp.Rows {
		if len(row.Elements) != len(chunk.req.Destinations) {
			return fmt.Errorf("API returned %d elements for row %d, expected %d",
				len(row.Elements), chunk.oStart+ri, len(chunk.req.Destinations))
		}
		for ci, el := range row.Elements {
			if err := mergeElement(result, chunk, ri, ci, el); err != nil {
				return err
			}
		}
	}
	return nil
}

func mergeElement(result *DistanceMatrixResult, chunk matrixChunk, ri, ci int, el DistanceMatrixElement) error {
	absRow, absCol := chunk.oStart+ri, chunk.dStart+ci
	if el.Status != "OK" {
		return fmt.Errorf("element [%d][%d] (%s -> %s) status: %s",
			absRow, absCol, chunk.req.Origins[ri], chunk.req.Destinations[ci], el.Status)
	}
	dur, err := elementDurationMinutes(el)
	if err != nil {
		return fmt.Errorf("element [%d][%d] (%s -> %s): %w",
			absRow, absCol, chunk.req.Origins[ri], chunk.req.Destinations[ci], err)
	}
	result.Response.Rows[absRow].Elements[absCol] = el
	result.Durations[absRow][absCol] = dur
	if el.Distance != nil {
		result.Distances[absRow][absCol] = el.Distance.Value
	}
	return nil
}
