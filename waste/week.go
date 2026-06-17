package waste

import (
	"context"
	"fmt"
)

// WeekReport is a full week of collection plans scored against the current
// vehicle assignment. The baseline is computed with the same midpoint +
// nearest-neighbour method as the plan, so the deadhead reduction is a like-for-
// like comparison rather than two methods measured differently.
type WeekReport struct {
	PerDay   []DayPlan
	Baseline WeekBaseline
	Totals   WeekTotals
}

// WeekBaseline is the current assignment scored across the week.
type WeekBaseline struct {
	TruckGroups        int
	AssignedUnits      int
	InterStopDeadheadM float64
}

// WeekTotals aggregates the planned week and its improvement over the baseline.
type WeekTotals struct {
	TruckShifts           int
	AssignedUnits         int
	UnassignedUnits       int
	DeadheadM             float64 // includes per-cell depot legs (solver objective)
	InterStopDeadheadM    float64 // between stops only; comparable to the baseline
	InterStopReductionPct float64 // positive = the plan cut deadhead vs the baseline
}

// AllWeekdays is the full Sunday-to-Saturday set PlanWeek plans.
var AllWeekdays = []int{0, 1, 2, 3, 4, 5, 6}

// PlanWeek plans every weekday, scores the current assignment baseline, and
// returns both with the totals that compare them.
func (p Planner) PlanWeek(ctx context.Context, units []RouteUnit) (WeekReport, error) {
	return p.PlanDays(ctx, units, AllWeekdays)
}

// PlanDays plans the given weekdays only, scoring the same baseline over the same
// days so a subset run (e.g. one day for a fast parameter sweep) stays an
// apples-to-apples comparison.
func (p Planner) PlanDays(ctx context.Context, units []RouteUnit, weekdays []int) (WeekReport, error) {
	report := WeekReport{PerDay: make([]DayPlan, 0, len(weekdays))}
	for _, weekday := range weekdays {
		plan, err := p.PlanDay(ctx, units, weekday)
		if err != nil {
			return WeekReport{}, fmt.Errorf("plan weekday %d: %w", weekday, err)
		}
		report.PerDay = append(report.PerDay, plan)
		report.Totals.TruckShifts += plan.Metrics.TrucksUsed
		report.Totals.AssignedUnits += plan.Metrics.AssignedUnits
		report.Totals.UnassignedUnits += plan.Metrics.UnassignedUnits
		report.Totals.DeadheadM += plan.Metrics.DeadheadM
		report.Totals.InterStopDeadheadM += plan.Metrics.InterStopDeadheadM
	}

	report.Baseline = p.baselineForDays(units, weekdays)
	report.Totals.InterStopReductionPct = reductionPct(
		report.Baseline.InterStopDeadheadM, report.Totals.InterStopDeadheadM)
	return report, nil
}

func (p Planner) baselineForDays(units []RouteUnit, weekdays []int) WeekBaseline {
	shift := p.Fleet.ShiftEnd - p.Fleet.ShiftStart
	var base WeekBaseline
	for _, weekday := range weekdays {
		day := BaselineDay(units, weekday, shift, p.Config)
		base.TruckGroups += day.TruckGroups
		base.AssignedUnits += day.AssignedUnits
		base.InterStopDeadheadM += day.InterStopDeadheadM
	}
	return base
}

// reductionPct reports how much the planned value cut the baseline, as a signed
// percentage rounded to one decimal (positive = reduction).
func reductionPct(baselineM, plannedM float64) float64 {
	if baselineM == 0 {
		return 0
	}
	return round1((baselineM - plannedM) / baselineM * 100)
}

func round1(v float64) float64 { return float64(int(v*10+0.5)) / 10 }
