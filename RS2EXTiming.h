//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-19.
//
//	Simulation timing constants.
//
//	RailSim II 2.15 used MAXFPS (lib/frame.h) for two unrelated purposes: the
//	render frame rate, and the rate at which the simulated world advances.
//	Because both were the same constant, changing the render frame rate would
//	silently change train speed, acceleration, the in-game clock and the wind.
//
//	SIMULATION_HZ below is the simulation rate and nothing else.  MAXFPS stays
//	the render/UI frame rate.  Both are 30 in v0.0.1, so behaviour is unchanged;
//	the point is that the two meanings are now separate symbols and can diverge
//	safely in a later version.

#ifndef RS2EX_TIMING_H_INCLUDED
#define RS2EX_TIMING_H_INCLUDED

namespace RS2EXTiming
{
	//	Legacy RailSim II simulation rate.
	//	Do not change without explicitly migrating simulation semantics:
	//	save files store CSaveFile::m_Frame in 1/SIMULATION_HZ second units.
	const int SIMULATION_HZ = 30;

	const double SIMULATION_STEP_MS =
		1000.0/static_cast<double>(SIMULATION_HZ);

	//	Prevent a long stall, breakpoint, modal window, or window deactivation
	//	from causing a huge burst of simulation updates.
	const int MAX_CATCH_UP_TICKS = 4;
}

#endif	//	RS2EX_TIMING_H_INCLUDED
