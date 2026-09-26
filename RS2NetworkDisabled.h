//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	The network sessions are gone (v0.2.0, user decision: removed, and not to
//	be reimplemented).
//
//	RailSim II 2.15 ran its network sessions on DirectPlay 8 (lib/comm.cpp).
//	DirectPlay's headers and libraries exist only in the DirectX 8 SDK, and a
//	failed InitDirectPlay stopped the program from starting.  v0.2.0 removed
//	DirectPlay, the start-up initialisation and the file-menu entries that
//	created or joined a session.  v0.3.0 removed the inherited session logic
//	(Network.cpp / Network.h) as well: see RS2NetworkDisabled.cpp for what the
//	remaining callers now link against.  No session can ever start, and
//	g_NetworkInitialized stays false.

#ifndef RS2NETWORKDISABLED_H_INCLUDED
#define RS2NETWORKDISABLED_H_INCLUDED

typedef DWORD DPNID;
#define DPNID_ALL_PLAYERS_GROUP	0

typedef BYTE RECEIVE_DATA;
typedef void (*PFN_RECEIVE)(RECEIVE_DATA *pData, DWORD dwSize, LPARAM lParam);

inline void SetReceiveFunc(PFN_RECEIVE, LPARAM){}
inline BOOL CreateSession(const GUID *, LPCTSTR, DWORD){ return FALSE; }
inline BOOL JoinSession(const GUID *, LPCTSTR, DWORD, DWORD){ return FALSE; }
inline BOOL CloseSession(){ return TRUE; }
inline BOOL EnumHosts(const GUID *, LPCTSTR){ return FALSE; }
inline BOOL SendToAll(const PVOID, DWORD){ return FALSE; }
inline BOOL SendTo(DPNID, const PVOID, DWORD){ return FALSE; }
inline DPNID GetLocalPlayerID(){ return 0; }
inline BOOL IsHost(){ return FALSE; }

//	What Network.h declared for the file mode, the interface modes and the
//	save file (v0.3.0: moved here with Network.h deleted).
class CListView;

enum RSNTransferState{
	RSN_TRANS_NONE = 0,
	RSN_TRANS_LAYOUT = 10,
	RSN_TRANS_FORCE_DWORD = 0x7fffffff,
};

int RSNCreateSession(int, int, const char *);
int RSNJoinSession(const char *, int, bool, const char *);
void RSNDeleteMember(DPNID id);
bool RSNCloseSession();
void RSNEndTransferData();
RSNTransferState GetNetworkTransferState();
bool IsNetworkTransferComplete();
bool CheckLayoutDigest(const unsigned char *);
inline bool CheckPortArea(int port){ return 0<=port && port<65536; }
void ExceedNetworkSyncLimit(int);
void ListNetworkMember(CListView *);

extern bool g_NetworkCloseRequest;
extern bool g_NetworkInitialized;
extern int g_NetworkSyncLimitReceived;
extern int g_NetworkSyncLimitSent;
extern int g_NetworkHostPort;
extern int g_NetworkLocalPort;
extern int g_NetworkSyncInterval;
extern char *g_NetworkFileCopy;
extern int g_NetworkFileCopySize;
extern int g_NetworkTransferSize;
extern int g_NetworkTransferRestSize;
extern char *g_NetworkTransferData;

#endif	//	RS2NETWORKDISABLED_H_INCLUDED
