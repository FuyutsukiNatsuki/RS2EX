//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-19.

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
