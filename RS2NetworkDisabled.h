//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	The network sessions are gone (v0.2.0, user decision: removed, and not to
//	be reimplemented).
//
//	RailSim II 2.15 ran its network sessions on DirectPlay 8 (lib/comm.cpp).
//	DirectPlay's headers and libraries exist only in the DirectX 8 SDK, and a
//	failed InitDirectPlay stopped the program from starting.  v0.2.0 removes
//	DirectPlay, the start-up initialisation and the file-menu entries that
//	created or joined a session.
//
//	What stays is the inherited session logic in Network.cpp, which the rest
//	of the game consults through g_NetworkInitialized: it is compiled against
//	these stand-ins for the DirectPlay calls, every one of which fails, so no
//	session can ever start and g_NetworkInitialized stays false.

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

#endif	//	RS2NETWORKDISABLED_H_INCLUDED
