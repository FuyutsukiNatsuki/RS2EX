//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-19.
//	Modified for RS2EX on 2026-09-20.
//
//	Fixed-rate simulation clock.
//
//	RailSim II 2.15 advanced the world exactly once per rendered frame, so the
//	simulation rate was whatever the render loop happened to achieve.  This class
//	answers one question and nothing else: how many SIMULATION_HZ ticks of real
//	time have elapsed since the last call.  Keeping it free of game state is what
//	lets the render rate change later without touching simulation behaviour.
//
//	Deliberately knows nothing about CSaveFile, CSimulationMode, Direct3D or
//	DirectPlay.

#ifndef CFIXEDSIMULATIONCLOCK_H_INCLUDED
#define CFIXEDSIMULATIONCLOCK_H_INCLUDED

class CFixedSimulationClock
{
private:
	LONGLONG m_LastCounter;	//	last HighTimer() sample
	double m_AccumulatorMs;	//	unconsumed real time
	bool m_Initialized;

public:
	CFixedSimulationClock();

	//	Discard elapsed time.
	//	prime=true lets the next ConsumeTicks() return one tick immediately,
	//	which preserves the legacy "first frame also simulates" behaviour.
	void Reset(bool prime = false);

	//	How many base ticks should be processed now (0..MAX_CATCH_UP_TICKS).
	int ConsumeTicks();

	//	How far the current render frame sits between the last completed tick and
	//	the next one, as 0..1.  Render-only: nothing in the simulation may read it.
	float GetInterpolationAlpha() const;
};

#endif	//	CFIXEDSIMULATIONCLOCK_H_INCLUDED
