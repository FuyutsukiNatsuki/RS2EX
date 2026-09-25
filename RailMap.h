//	Modified for RS2EX on 2026-09-26.
#ifndef RAILMAP_H_INCLUDED
#define RAILMAP_H_INCLUDED

void InitRailMap();
void DumpMapLine(VEC2, RS2PackedColor, VEC2, RS2PackedColor, bool shadow = true);
void RailMapLine(VEC3, RS2PackedColor, VEC3, RS2PackedColor, bool shadow = true, bool bold = false);
void RailMapText(VEC3, char *, RS2PackedColor);
void RenderRailMap();

extern bool g_MapDrawNeeded;

#endif
