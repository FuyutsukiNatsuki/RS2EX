//	Modified for RS2EX on 2026-09-20, 2026-09-21, 2026-09-26.
#pragma warning (disable: 4786)

#include <string>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <algorithm>
#include "lib\udx.h"
#include "RS2MaterialBinding.h"
#include "RS2Lighting.h"
#include "RS2Text.h"

typedef list<string>::iterator Istring;

using namespace std;

#include "Const.h"
#include "Macro.h"
#include "Language.h"
#include "ValueArea.h"
#include "SystemCover.h"
#include "GraphicCover.h"
#include "Script.h"
#include "RS2SaveRef.h"	//	[RS2EX] v0.3.0
#include "CWaveArray.h"
#include "CVertexDump.h"
#include "CStringTexture.h"

extern char g_BaseDir[];
extern int g_DispWidth;
extern int g_DispHeight;
extern int g_RSPV;
extern CTexList g_TexList;
extern CMeshList g_MeshList;
extern bool g_NetworkInitialized;
extern bool g_ManualControl;