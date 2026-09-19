//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-19.
//	Modified for RS2EX on 2026-09-20.

#include "stdafx.h"
#include "HighTimer.h"
#include "RS2EXTiming.h"
#include "CFixedSimulationClock.h"

/*
 *	constructor
 */
CFixedSimulationClock::CFixedSimulationClock()
	: m_LastCounter(0),
	  m_AccumulatorMs(0.0),
	  m_Initialized(false)
{
}

/*
 *	Discard elapsed real time.
 *
 *	prime	: true to make the next ConsumeTicks() yield one tick at once
 */
void CFixedSimulationClock::Reset(
	bool prime	//	prime the accumulator
){
	m_LastCounter = HighTimer();
	m_AccumulatorMs = prime ? RS2EXTiming::SIMULATION_STEP_MS : 0.0;
	m_Initialized = true;
}

/*
 *	How many base ticks of real time have elapsed.
 *
 *	returns	: tick count, clamped to RS2EXTiming::MAX_CATCH_UP_TICKS
 */
int CFixedSimulationClock::ConsumeTicks(){
	if(!m_Initialized) Reset(true);

	const LONGLONG now = HighTimer();
	double elapsedMs = FromHighTimerCountToMs(now-m_LastCounter);

	m_LastCounter = now;

	//	QueryPerformanceCounter can step backwards across cores on old hardware.
	if(elapsedMs<0.0) elapsedMs = 0.0;

	//	A breakpoint, a modal dialog, a sleeping PC or a slow plugin load must not
	//	turn into hundreds of catch-up ticks.  Long stalls are dropped, which is
	//	what CFrame::Sync() already does for the render timeline.
	const double maxAccumulatedMs =
		RS2EXTiming::SIMULATION_STEP_MS*RS2EXTiming::MAX_CATCH_UP_TICKS;

	if(elapsedMs>maxAccumulatedMs) elapsedMs = maxAccumulatedMs;

	m_AccumulatorMs += elapsedMs;
	if(m_AccumulatorMs>maxAccumulatedMs) m_AccumulatorMs = maxAccumulatedMs;

	int ticks = 0;
	while(m_AccumulatorMs>=RS2EXTiming::SIMULATION_STEP_MS
		&& ticks<RS2EXTiming::MAX_CATCH_UP_TICKS){
		m_AccumulatorMs -= RS2EXTiming::SIMULATION_STEP_MS;
		ticks++;
	}
	return ticks;
}

/*
 *	How far this render frame sits between simulation states.
 *
 *	returns	: 0.0 just after a tick, approaching 1.0 just before the next one
 *
 *	ConsumeTicks() leaves the accumulator holding the real time that has passed
 *	since the last completed tick, which is exactly the fraction the renderer
 *	needs to blend the previous and current simulation states.
 *
 *	Deriving the fraction from real time rather than counting render frames is
 *	deliberate: it keeps working if the render target stops being twice the
 *	simulation rate, and it does not assume an exact frame cadence.
 *
 *	Clamped, so a timer anomaly cannot turn into extrapolation past the current
 *	state.
 */
float CFixedSimulationClock::GetInterpolationAlpha() const{
	if(!m_Initialized) return 0.0f;
	const double alpha = m_AccumulatorMs/RS2EXTiming::SIMULATION_STEP_MS;
	if(alpha<=0.0) return 0.0f;
	if(alpha>=1.0) return 1.0f;
	return (float)alpha;
}
