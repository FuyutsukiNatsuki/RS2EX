//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	-x64smoke: pointer-width correctness with a real high address (v0.3.0 WP5,
//	plan section 23).
//
//	An x64 process usually gets low addresses for most of its allocations, so
//	a pointer squeezed through a 32-bit slot can survive by luck.  This mode
//	takes that luck away: it obtains one small committed range whose address
//	has bits above 32 (the sentinel) and sends addresses inside it through the
//	contracts v0.3.0 fixed:
//
//	    types        the LLP64 sizes the code relies on;
//	    list         CListElement data, on an element that itself lives at the
//	                 high address;
//	    drag         the real CListView::PrepareDrag -> CDragContainer path;
//	    dispatch     CMenuCommander::Dispatch / DoubleClick with the element
//	                 pointer, as CListView / CTreeFileElement pass it;
//	    Windows      the pointer contexts the program hands to Windows and
//	                 DirectX and gets back: the DirectInput EnumObjects context
//	                 (InitJoyStick) and the CCrtThread start parameter (the
//	                 input polling thread);
//	    save refs    a small object graph with one object at the high address
//	                 saves to logical IDs - the same text as the same graph at
//	                 low addresses, no trace of either address - and loads back
//	                 to the right objects.
//
//	Every received value is compared to the exact address sent and read
//	through, which only ever touches the committed sentinel.  The sentinel is
//	requested at fixed candidate addresses (below) and then top-down; if none
//	lies above 4 GiB the result is "environment-limited", never "pass".  The
//	range is released and checked free at the end.
//
//	A 32-bit build runs the same contracts at ordinary addresses and reports
//	the high-address sections as skipped: a 32-bit process has no such
//	address, and casting a 64-bit constant to a pointer would prove nothing.

#include "stdafx.h"
#include "CListView.h"
#include "RS2SaveRef.h"
#include "RS2X64Smoke.h"
#include <new>

#ifndef IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA
#define IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA 0x0020
#endif

static const DWORD RS2_X64_MAGIC = 0x58325352;	//	"RS2X"

//	Where the sentinel is asked for, in order.  6 GiB first: its low half has
//	bit 31 set as well, so a truncation to 32 bits and a sign extension back
//	both change the address.  The last one is below the 128 TiB user-mode
//	limit of Windows 8.1 and later.
static const unsigned long long s_RS2X64Candidates[] = {
	0x0000000180000000ULL,	//	6 GiB
	0x0000000100000000ULL,	//	4 GiB
	0x0000000200000000ULL,	//	8 GiB
	0x0000001000000000ULL,	//	64 GiB
	0x0000010000000000ULL,	//	1 TiB
	0x00007f0000000000ULL,	//	127 TiB
};

//	One allocation granule, and where things sit inside it.  None of the
//	offsets is 0, so the low half of every tested address is not 0 either.
static const SIZE_T RS2_X64_SENTINEL_BYTES = 0x10000;
static const SIZE_T RS2_X64_MARKER_OFFSET = 0x1230;
static const SIZE_T RS2_X64_ELEMENT_OFFSET = 0x2000;
static const SIZE_T RS2_X64_NODE_OFFSET = 0x3000;
static const SIZE_T RS2_X64_LOADED_OFFSET = 0x4000;

struct RS2X64Marker{
	DWORD magic;
	DWORD serial;
	const void *self;
};

struct RS2X64Node{
	RS2X64Marker marker;
	RS2X64Node *next;
};

bool RS2X64SmokeRequested(){
	return CheckArguments("-x64smoke")!=FALSE;
}

static void RS2X64Step(const char *name, bool ok, bool *all){
	Debug("RS2X64SMOKE|%-56s|%s\n", name, ok ? "pass" : "FAIL");
	if(!ok) *all = false;
}

static void RS2X64Skip(const char *name){
	Debug("RS2X64SMOKE|%-56s|skip (32-bit process)\n", name);
}

static void RS2X64Size(const char *type, size_t got, size_t want, bool *all){
	char name[64];
	_snprintf(name, sizeof(name), "sizeof(%s) == %u", type, (unsigned int)want);
	name[sizeof(name)-1] = 0;
	RS2X64Step(name, got==want, all);
}

static bool RS2X64IsMarker(const void *p, const void *expected){
	const RS2X64Marker *m = (const RS2X64Marker *)p;
	return p==expected && m->magic==RS2_X64_MAGIC && m->self==expected;
}

static void RS2X64InitMarker(RS2X64Marker *m){
	m->magic = RS2_X64_MAGIC;
	m->serial = 0;
	m->self = m;
}

static bool RS2X64IsHigh(const void *p){
	return ((unsigned long long)(uintptr_t)p>>32)!=0;
}

//	---- sentinel ----------------------------------------------------------------

#ifdef _WIN64
static BYTE *RS2X64AcquireSentinel(){
	size_t i;
	for(i = 0; i<sizeof(s_RS2X64Candidates)/sizeof(s_RS2X64Candidates[0]); i++){
		const unsigned long long want = s_RS2X64Candidates[i];
		void *p = VirtualAlloc((void *)(uintptr_t)want, RS2_X64_SENTINEL_BYTES,
			MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
		if(p && (unsigned long long)(uintptr_t)p==want){
			Debug("RS2X64SMOKE|sentinel candidate 0x%016llx: acquired\n", want);
			return (BYTE *)p;
		}
		if(p) VirtualFree(p, 0, MEM_RELEASE);
		Debug("RS2X64SMOKE|sentinel candidate 0x%016llx: unavailable\n", want);
	}
	void *p = VirtualAlloc(NULL, RS2_X64_SENTINEL_BYTES,
		MEM_RESERVE|MEM_COMMIT|MEM_TOP_DOWN, PAGE_READWRITE);
	if(p && RS2X64IsHigh(p)){
		Debug("RS2X64SMOKE|sentinel top-down: acquired 0x%016llx\n", (unsigned long long)(uintptr_t)p);
		return (BYTE *)p;
	}
	Debug("RS2X64SMOKE|sentinel top-down: %s\n", p ? "below 4 GiB" : "unavailable");
	if(p) VirtualFree(p, 0, MEM_RELEASE);
	return NULL;
}
#endif

//	---- list / drag / dispatch ----------------------------------------------------

class RS2X64ProbeCommander: public CMenuCommander{
public:
	CMDTYPE m_Type;
	RS2OpaqueData m_Dispatched, m_DoubleClicked;
	RS2X64ProbeCommander(): m_Type(CMD_NONE), m_Dispatched(0), m_DoubleClicked(0){}
	CPopMenu *Dispatch(CMDTYPE type, RS2OpaqueData data){
		m_Type = type;
		m_Dispatched = data;
		return NULL;
	}
	void DoubleClick(CMDTYPE type, RS2OpaqueData data){
		m_Type = type;
		m_DoubleClicked = data;
	}
};

//	A list view that is never shown: the list and drag code of CListView as it
//	is, with the element storage owned by the smoke (placed in the sentinel).
class RS2X64ProbeListView: public CListView{
public:
	RS2X64ProbeListView(CMenuCommander *cmd){
		m_State = 0;
		m_Cols = 1;
		m_FocusIndex = 0;
		m_DragType = DRAG_INSERT;
		m_Commander = cmd;
		m_CmdType = CMD_FILE;
	}
	~RS2X64ProbeListView(){ Detach(); }
	void Detach(){
		m_Data = m_FocusItem = NULL;
		m_ItemNum = 0;
	}
	//	CListView::ScanInput, right button: the element under the cursor
	CPopMenu *DispatchFocus(){
		CListElement *popitem = GetElement(m_FocusIndex);
		return m_Commander->Dispatch(m_CmdType, (RS2OpaqueData)popitem);
	}
	//	CTreeFileElement::ScanInput, double click: the object behind the element
	void DoubleClickFocus(){
		m_Commander->DoubleClick(m_CmdType, GetElement(m_FocusIndex)->GetData());
	}
};

static void RS2X64Contracts(const char *where, void *elementMemory, RS2X64Marker *marker, bool *all){
	char name[96], text[] = "x64smoke";
	const RS2OpaqueData data = (RS2OpaqueData)marker;
	RS2X64ProbeCommander cmd;
	RS2X64ProbeListView view(&cmd);

	CListElement *element = new(elementMemory) CListElement(1, text, &view);
	view.InsertItem(0, element);
	element->SetData(data);

	CListElement *got = view.GetElement(0);
	_snprintf(name, sizeof(name), "%s: list element data", where);
	name[sizeof(name)-1] = 0;
	RS2X64Step(name, got==element && got->GetData()==data && RS2X64IsMarker((const void *)got->GetData(), marker), all);

	view.PrepareDrag();
	bool drag = CDragContainer::IsDragging() && CDragContainer::GetType()==DRAG_INSERT
		&& CDragContainer::GetOwner()==&view && CDragContainer::GetData().size()==1
		&& CDragContainer::GetData().front()==data
		&& RS2X64IsMarker((const void *)CDragContainer::GetData().front(), marker);
	CDragContainer::EndDrag();
	_snprintf(name, sizeof(name), "%s: drag data (CListView::PrepareDrag)", where);
	name[sizeof(name)-1] = 0;
	RS2X64Step(name, drag && !CDragContainer::IsDragging(), all);

	CPopMenu *pop = view.DispatchFocus();
	CListElement *back = (CListElement *)cmd.m_Dispatched;
	_snprintf(name, sizeof(name), "%s: Dispatch data (element pointer)", where);
	name[sizeof(name)-1] = 0;
	RS2X64Step(name, !pop && cmd.m_Type==CMD_FILE && back==element
		&& back->GetData()==data && RS2X64IsMarker((const void *)back->GetData(), marker), all);

	view.DoubleClickFocus();
	_snprintf(name, sizeof(name), "%s: DoubleClick data (object pointer)", where);
	name[sizeof(name)-1] = 0;
	RS2X64Step(name, cmd.m_DoubleClicked==data && RS2X64IsMarker((const void *)cmd.m_DoubleClicked, marker), all);

	view.Detach();
	element->~CListElement();
}

//	---- Windows / DirectX pointer contexts ----------------------------------------

static const void *s_RS2X64DIContext;
static int s_RS2X64DICalls;

static BOOL CALLBACK RS2X64EnumObject(LPCDIDEVICEOBJECTINSTANCE, LPVOID pvRef){
	s_RS2X64DIContext = pvRef;
	s_RS2X64DICalls++;
	return DIENUM_STOP;
}

static bool RS2X64DIContext(const void *context){
	if(!svi.pKey) return false;
	s_RS2X64DIContext = NULL;
	s_RS2X64DICalls = 0;
	const HRESULT hr = svi.pKey->EnumObjects(RS2X64EnumObject, (VOID *)context, DIDFT_ALL);
	return SUCCEEDED(hr) && s_RS2X64DICalls==1 && s_RS2X64DIContext==context;
}

static const void *volatile s_RS2X64ThreadParam;

static unsigned int __stdcall RS2X64Thread(void *p){
	s_RS2X64ThreadParam = p;
	((RS2X64Marker *)p)->serial++;
	return 0;
}

static bool RS2X64ThreadParam(RS2X64Marker *marker){
	CCrtThread thread;
	const DWORD before = marker->serial;
	s_RS2X64ThreadParam = NULL;
	const bool started = thread.begin(RS2X64Thread, marker);
	thread.end(5000);
	return started && s_RS2X64ThreadParam==marker && marker->serial==before+1;
}

static void RS2X64Windows(const char *where, RS2X64Marker *marker, bool *all){
	char name[96];
	_snprintf(name, sizeof(name), "%s: DirectInput EnumObjects context", where);
	name[sizeof(name)-1] = 0;
	RS2X64Step(name, RS2X64DIContext(marker) && RS2X64IsMarker(s_RS2X64DIContext, marker), all);
	_snprintf(name, sizeof(name), "%s: CCrtThread start parameter", where);
	name[sizeof(name)-1] = 0;
	RS2X64Step(name, RS2X64ThreadParam(marker), all);
}

//	---- save references -------------------------------------------------------------

//	The graph a -> b -> a, saved the way CSaveFile::Save does: a numbering pass,
//	then the real one.
static void RS2X64SaveGraph(const RS2X64Node *a, const RS2X64Node *b, string &out){
	const RS2X64Node *nodes[2] = {a, b};
	char line[64];
	int pass, i;
	RS2SaveRefBeginSave();
	for(pass = 0; pass<2; pass++){
		RS2SaveRefSetNumbering(pass==0);
		out.clear();
		for(i = 0; i<2; i++){
			_snprintf(line, sizeof(line), "Address = " RS2_SAVEREF_FMT ";\n", RS2SaveRefDefine(nodes[i]));
			line[sizeof(line)-1] = 0;
			out += line;
			_snprintf(line, sizeof(line), "Next = " RS2_SAVEREF_FMT ";\n", RS2SaveRefOf(nodes[i]->next));
			line[sizeof(line)-1] = 0;
			out += line;
		}
	}
	RS2SaveRefEndSave();
}

static bool RS2X64Contains(const string &text, const char *fmt, unsigned long long v){
	char s[40];
	_snprintf(s, sizeof(s), fmt, v);
	s[sizeof(s)-1] = 0;
	return text.find(s)!=string::npos;
}

//	Load the text into two new nodes: register, park the references, resolve.
static bool RS2X64LoadGraph(const string &text, RS2X64Node *loaded[2]){
	char address[] = "Address", next[] = "Next";
	vector<char> buf(text.begin(), text.end());
	buf.push_back(0);
	char *line = &buf[0];
	int i;
	RS2SaveRefBeginLoad();
	RS2SaveRefSetSyntax(RS2_SAVEREF_DECIMAL);
	for(i = 0; i<2; i++){
		RS2SaveRef ref = 0;
		char *eol = strchr(line, '\n');
		if(!eol) return false;
		*eol = 0;
		if(!RS2AsgnSaveRef(line, address, &ref) || !RS2SaveRefRegister(ref, loaded[i])) return false;
		line = eol+1;
		if(!(eol = strchr(line, '\n'))) return false;
		*eol = 0;
		if(!RS2AsgnSaveRefSlot(line, next, (void **)&loaded[i]->next)) return false;
		line = eol+1;
	}
	for(i = 0; i<2; i++)
		loaded[i]->next = (RS2X64Node *)RS2SaveRefResolve(RS2SaveRefFromSlot(loaded[i]->next));
	return RS2SaveRefUnresolvedCount()==0;
}

static void RS2X64SaveRefs(const char *where, RS2X64Node *a, RS2X64Node *loadedA, bool *all){
	static const char *const expected = "Address = 1;\nNext = 2;\nAddress = 2;\nNext = 1;\n";
	char name[96];
	RS2X64Node b, loadedB, lowA;
	RS2X64InitMarker(&a->marker);
	RS2X64InitMarker(&b.marker);
	RS2X64InitMarker(&lowA.marker);
	a->next = &b;
	b.next = a;
	lowA.next = &b;

	string text, lowText;
	RS2X64SaveGraph(a, &b, text);
	_snprintf(name, sizeof(name), "%s: saved as logical IDs 1, 2", where);
	name[sizeof(name)-1] = 0;
	RS2X64Step(name, text==expected, all);

	const unsigned long long addr = (unsigned long long)(uintptr_t)a;
	_snprintf(name, sizeof(name), "%s: no address in the text", where);
	name[sizeof(name)-1] = 0;
	RS2X64Step(name, !RS2X64Contains(text, "%llu", addr) && !RS2X64Contains(text, "%llX", addr)
		&& !RS2X64Contains(text, "%llx", addr), all);

	//	The same graph with a at an ordinary heap address saves identically.
	b.next = &lowA;
	RS2X64SaveGraph(&lowA, &b, lowText);
	b.next = a;
	_snprintf(name, sizeof(name), "%s: text independent of the address", where);
	name[sizeof(name)-1] = 0;
	RS2X64Step(name, text==lowText, all);

	RS2X64Node *loaded[2] = {loadedA, &loadedB};
	RS2X64InitMarker(&loadedA->marker);
	RS2X64InitMarker(&loadedB.marker);
	loadedA->next = loadedB.next = NULL;
	const bool read = RS2X64LoadGraph(text, loaded);
	_snprintf(name, sizeof(name), "%s: loads back to the new objects", where);
	name[sizeof(name)-1] = 0;
	RS2X64Step(name, read && loadedA->next==&loadedB && loadedB.next==loadedA
		&& RS2X64IsMarker(&loadedB.next->marker, loadedA), all);

	RS2SaveRefBeginLoad();	//	leave no table entry pointing at the sentinel
}

//	---- run ---------------------------------------------------------------------------

bool RS2X64SmokeRun(){
	bool all = true;
#ifdef _WIN64
	const size_t ptr = 8;
#else
	const size_t ptr = 4;
#endif

	Debug("RS2X64SMOKE|process: %d-bit\n", (int)(sizeof(void *)*8));
	{
		const BYTE *image = (const BYTE *)GetModuleHandle(NULL);
		const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(image+((const IMAGE_DOS_HEADER *)image)->e_lfanew);
		const bool laa = (nt->FileHeader.Characteristics&IMAGE_FILE_LARGE_ADDRESS_AWARE)!=0;
		const bool heva = (nt->OptionalHeader.DllCharacteristics&IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA)!=0;
		Debug("RS2X64SMOKE|image: large address aware=%d high entropy VA=%d\n", laa ? 1 : 0, heva ? 1 : 0);
#ifdef _WIN64
		RS2X64Step("executable is large-address aware", laa, &all);
#endif
	}

	//	---- types
	RS2X64Size("void *", sizeof(void *), ptr, &all);
	RS2X64Size("uintptr_t", sizeof(uintptr_t), ptr, &all);
	RS2X64Size("intptr_t", sizeof(intptr_t), ptr, &all);
	RS2X64Size("size_t", sizeof(size_t), ptr, &all);
	RS2X64Size("RS2OpaqueData", sizeof(RS2OpaqueData), ptr, &all);
	RS2X64Size("LPARAM", sizeof(LPARAM), ptr, &all);
	RS2X64Size("DWORD", sizeof(DWORD), 4, &all);
	RS2X64Size("long", sizeof(long), 4, &all);
	RS2X64Size("int", sizeof(int), 4, &all);
	RS2X64Size("RS2SaveRef", sizeof(RS2SaveRef), 8, &all);

	//	---- ordinary addresses (both builds)
	{
		RS2X64Marker *marker = new RS2X64Marker;
		void *element = ::operator new(sizeof(CListElement));
		RS2X64InitMarker(marker);
		RS2X64Contracts("heap", element, marker, &all);
		RS2X64Windows("heap", marker, &all);
		::operator delete(element);
		delete marker;

		RS2X64Step("DirectInput joystick-number context (InitJoyStick)",
			RS2X64DIContext((VOID *)(INT_PTR)(MAX_JOYSTICK-1))
			&& (int)(INT_PTR)s_RS2X64DIContext==MAX_JOYSTICK-1, &all);

		RS2X64Node *a = new RS2X64Node, *loadedA = new RS2X64Node;
		RS2X64SaveRefs("heap", a, loadedA, &all);
		delete a;
		delete loadedA;
	}

	//	---- the high sentinel (x64 only)
	static const char *const highSections[] = {
		"sentinel above 4 GiB",
		"sentinel: list / drag / Dispatch / DoubleClick",
		"sentinel: DirectInput context / CCrtThread parameter",
		"sentinel: save references",
		"sentinel released",
	};
#ifdef _WIN64
	(void)highSections;
	BYTE *base = RS2X64AcquireSentinel();
	RS2X64Step("sentinel above 4 GiB", base && RS2X64IsHigh(base), &all);
	if(!base){
		Debug("RS2X64SMOKE|result|environment-limited: no allocation above 4 GiB was available (not a pass)\n");
		return false;
	}
	Debug("RS2X64SMOKE|sentinel 0x%016llx, %u bytes committed\n",
		(unsigned long long)(uintptr_t)base, (unsigned int)RS2_X64_SENTINEL_BYTES);

	RS2X64Marker *marker = (RS2X64Marker *)(base+RS2_X64_MARKER_OFFSET);
	RS2X64InitMarker(marker);
	RS2X64Step("sentinel marker has high and low bits",
		RS2X64IsHigh(marker) && ((uintptr_t)marker&0xffffffffu)!=0, &all);
	RS2X64Contracts("sentinel", base+RS2_X64_ELEMENT_OFFSET, marker, &all);
	RS2X64Windows("sentinel", marker, &all);
	RS2X64SaveRefs("sentinel", (RS2X64Node *)(base+RS2_X64_NODE_OFFSET),
		(RS2X64Node *)(base+RS2_X64_LOADED_OFFSET), &all);

	const BOOL freed = VirtualFree(base, 0, MEM_RELEASE);
	MEMORY_BASIC_INFORMATION mbi;
	const bool released = freed && VirtualQuery(base, &mbi, sizeof(mbi))==sizeof(mbi) && mbi.State==MEM_FREE;
	RS2X64Step("sentinel released (VirtualQuery: MEM_FREE)", released, &all);
#else
	size_t i;
	for(i = 0; i<sizeof(highSections)/sizeof(highSections[0]); i++) RS2X64Skip(highSections[i]);
#endif
	RS2X64Step("no drag left behind", !CDragContainer::IsDragging(), &all);

#ifdef _WIN64
	Debug("RS2X64SMOKE|result|%s\n", all ? "pass" : "FAIL");
#else
	Debug("RS2X64SMOKE|result|%s (high-address sections skipped: 32-bit process)\n", all ? "pass" : "FAIL");
#endif
	return all;
}
