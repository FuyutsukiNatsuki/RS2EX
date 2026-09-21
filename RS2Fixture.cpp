//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	See RS2Fixture.h for why this exists.

#include "stdafx.h"
#include "RS2Fixture.h"

//	One simulated second at SIMULATION_HZ.  Enough for the loaded layout to
//	settle; short enough that a capture does not wait for it.
static const int RS2_FIXTURE_SETTLE_TICKS = 30;

static int s_TicksRun = 0;

bool RS2FixtureIsEnabled(){
	//	Resolved once.  CheckArguments walks the command line every call, and a
	//	mode that could switch on part-way through a run would not be
	//	deterministic at all.
	static int s_Enabled = -1;

	if(s_Enabled<0){
		s_Enabled = CheckArguments("-fixture") ? 1 : 0;
		if(s_Enabled)
			Debug("[RS2EX Fixture] deterministic mode: the world stops after %d ticks\n",
				RS2_FIXTURE_SETTLE_TICKS);
	}
	return s_Enabled!=0;
}

bool RS2FixtureIsFrozen(){
	return RS2FixtureIsEnabled() && s_TicksRun>=RS2_FIXTURE_SETTLE_TICKS;
}

int RS2FixtureClampTicks(
	int offered		//	ticks the clock has accumulated
){
	const int remaining = RS2_FIXTURE_SETTLE_TICKS-s_TicksRun;

	if(offered>remaining) offered = remaining;
	if(offered<=0) return 0;

	s_TicksRun += offered;
	if(s_TicksRun>=RS2_FIXTURE_SETTLE_TICKS)
		Debug("[RS2EX Fixture] world stopped after %d ticks\n", s_TicksRun);
	return offered;
}
