package timeWindow

// Overlaps returns true if time ranges [aStart, aEnd) and [bStart, bEnd) share any time.
func Overlaps(aStart, aEnd, bStart, bEnd int) bool {
	return aStart < bEnd && bStart < aEnd
}
