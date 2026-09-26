//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	What remains of the network sessions: nothing that can run (v0.3.0).
//
//	v0.2.0 removed DirectPlay and every way to start a session; v0.3.0 removes
//	the inherited session logic itself (Network.cpp, user decision), because
//	porting a permanently removed protocol to x64 would be work on code that
//	cannot run.  About twenty inherited files still consult the network state
//	(g_NetworkInitialized) and call its entry points from branches that are
//	only taken inside a session.  Those branches are left alone; this file
//	gives the names they link against a session that never exists:
//
//	    g_NetworkInitialized is false and nothing sets it,
//	    creating or joining a session fails (3, "failed", as before),
//	    there is never a transfer, and every other entry point does nothing.
//
//	The initial values are Network.cpp's, so the network dialogs that can no
//	longer be reached would still show what they showed.

#include "stdafx.h"
#include "RS2EXTiming.h"

extern const int RSN_SCRNAME_MAX = 32;
extern const int RSN_SYNC_INTERVAL_MIN = RS2EXTiming::SIMULATION_HZ/10;
extern const int RSN_SYNC_INTERVAL_MAX = RS2EXTiming::SIMULATION_HZ*5;

bool g_NetworkCloseRequest = false;
bool g_NetworkInitialized = false;
int g_NetworkSyncLimitReceived = 0;
int g_NetworkSyncLimitSent = 0;
int g_NetworkHostPort = 0;
int g_NetworkLocalPort = 0;
int g_NetworkSyncInterval = 0;
char *g_NetworkFileCopy = NULL;
int g_NetworkFileCopySize = 0;
int g_NetworkTransferSize = 0;
int g_NetworkTransferRestSize = 0;
char *g_NetworkTransferData = NULL;

int g_LastJoinedHostIP[4] = {0, 0, 0, 0};
int g_LastJoinedHostPort = 51111;
int g_LastJoinedLocalPort = 51112;
int g_LastCreatedHostPort = 51111;
string g_LastScreenName;

int g_NetworkDummyMapAddress = 1;

int RSNCreateSession(int, int, const char *){ return 3; }
int RSNJoinSession(const char *, int, bool, const char *){ return 3; }
void RSNDeleteMember(DPNID){}
bool RSNCloseSession(){ return false; }
void RSNEndTransferData(){}
RSNTransferState GetNetworkTransferState(){ return RSN_TRANS_NONE; }
bool IsNetworkTransferComplete(){ return false; }
bool CheckLayoutDigest(const unsigned char *){ return false; }
void ExceedNetworkSyncLimit(int){}
void ListNetworkMember(CListView *){}

void CheckNetworkState(){}
void InitNetworkInterface(){}
bool ScanInputNetworkInterface(){ return false; }
void RenderNetworkInterface(){}
void StartNetworkSimulation(){}
void UpdateSyncLimit(){}
void SyncTrainControls(int){}
void PushChatLog(char *, DWORD){}

void EnqueueSwitchControl(int, int){}
void EnqueuePointControl(void *, int){}
void EnqueueSetTrainControl(void *, void *, int, float, int){}
void EnqueueMergeTrainControl(void *, void *, int, int){}
