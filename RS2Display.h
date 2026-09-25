//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	The display size: the one answer to "how big is the screen" (v0.2.0).
//
//	RailSim II asked g_DispWidth / g_DispHeight, and those were the configured
//	resolution for the whole run.  That held while the back buffer was always
//	that size.  From v0.2.0 it is not: borderless fullscreen presents at the
//	monitor's own resolution, and a window whose requested size does not fit
//	is given a smaller client area by Windows.  The back buffer is the size of
//	the client area, and the UI, the cursor and the camera have to agree with
//	it or the picture is stretched and the mouse misses what it points at.
//
//	So the renderer backend publishes the back-buffer size here whenever it
//	creates or resizes the swap chain, and g_DispWidth / g_DispHeight are
//	updated from it.  The ~145 inherited readers keep reading those globals and
//	get the live size without being touched.  The configured resolution is the
//	window's requested initial size and lives in CConfigMode; it is no longer
//	the same thing.
//
//	The size is decided at start-up (user decision, 2026-09-26: no in-game
//	resolution change in v0.2.0).  A change after that - only ever from
//	outside the program, since the window has no sizing frame - still keeps
//	everything consistent: the generation below counts changes, and the few
//	things laid out once (tool windows, the cursor clip) use it to notice.
//
//	Aspect: the camera takes its aspect from the viewport it draws into
//	(CCamera::ApplyProjection), which is the whole display or one division of
//	it.  RS2GetDisplayAspect() is the aspect of the whole display, for anything
//	that needs that instead.  Neither is ever a 4:3 constant.

#ifndef RS2DISPLAY_H_INCLUDED
#define RS2DISPLAY_H_INCLUDED

//	Called by the renderer backend with the back-buffer size.  Non-positive
//	sizes (a minimised window) are ignored: the display keeps its last size.
void RS2PublishDisplaySize(int width, int height);

int RS2GetDisplayWidth();
int RS2GetDisplayHeight();
float RS2GetDisplayAspect();

//	Incremented every time the published size actually changes, starting at 0
//	before the first publication.
unsigned int RS2GetDisplayGeneration();

#endif	//	RS2DISPLAY_H_INCLUDED
