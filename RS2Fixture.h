//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-21.
//
//	Deterministic capture mode, for renderer validation.
//
//	The scene RailSim normally shows is not reproducible: the sky changes
//	colour with the in-game clock and the train and camera move, so the same
//	binary photographed twice differs from itself in most pixels.  That is why
//	v0.0.9 retired pixel comparison, and why it then shipped a candidate whose
//	3D had entirely disappeared - the structural check that replaced it can
//	tell that a scene is there, but not that it is right.
//
//	-fixture runs a fixed number of simulation ticks and then stops the world.
//	Every frame after that renders the same state, so two runs produce
//	identical pixels and a change that alters the image is visible exactly.
//
//	It is a developer switch: not in the settings, not saved, and when it is
//	absent nothing here has any effect.

#ifndef RS2FIXTURE_H_INCLUDED
#define RS2FIXTURE_H_INCLUDED

/*
 *	Whether -fixture was given.
 *
 *	Read once and cached, so the answer cannot change part-way through a run.
 */
bool RS2FixtureIsEnabled();

/*
 *	Whether the world has stopped.
 *
 *	False when -fixture was not given, and false while the settle ticks are
 *	still running.  Rendering that is driven by real time rather than by the
 *	simulation has to consult this too, or the picture keeps moving after the
 *	simulation has stopped.
 */
bool RS2FixtureIsFrozen();

/*
 *	How many of the ticks the clock is offering may actually run.
 *
 *	The clock can offer several at once after a stall, so an unclamped total
 *	would overshoot the budget by a different amount every run and the world
 *	would freeze in a slightly different state each time.
 *
 *	offered	: ticks the simulation clock has accumulated
 *	returns	: ticks to run now, 0 once the budget is spent
 */
int RS2FixtureClampTicks(int offered);

#endif	//	RS2FIXTURE_H_INCLUDED
