//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.

#include "stdafx.h"
#include "RS2RenderResource.h"

/*
 *	The registry instance
 *
 *	Never deleted - see the header for why.
 */
CRS2ResetRegistry &GetRS2ResetRegistry(){
	static CRS2ResetRegistry *registry = new CRS2ResetRegistry;
	return *registry;
}

CRS2ResetRegistry::CRS2ResetRegistry()
	: m_Count(0),
	  m_BeforeResetCalls(0),
	  m_AfterResetCalls(0),
	  m_RecreateFailures(0)
{
	int i;
	for(i = 0; i<MAX_PARTICIPANTS; i++) m_Participants[i] = NULL;
}

/*
 *	Add a participant
 *
 *	Registering twice would mean releasing twice, so duplicates are ignored
 *	rather than trusted not to happen.
 */
void CRS2ResetRegistry::Register(IRS2RendererResetParticipant *participant){
	if(!participant) return;

	int i;
	for(i = 0; i<m_Count; i++)
		if(m_Participants[i]==participant) return;

	if(m_Count>=MAX_PARTICIPANTS){
		Debug("[RS2EX Resource] reset participant registry full\n");
		return;
	}
	m_Participants[m_Count++] = participant;
}

/*
 *	Remove a participant
 *
 *	Called from destructors, so it must tolerate a participant that was never
 *	registered.  The last entry is moved into the gap; order does not matter
 *	because no participant depends on another.
 */
void CRS2ResetRegistry::Unregister(IRS2RendererResetParticipant *participant){
	if(!participant) return;

	int i;
	for(i = 0; i<m_Count; i++){
		if(m_Participants[i]!=participant) continue;
		m_Participants[i] = m_Participants[--m_Count];
		m_Participants[m_Count] = NULL;
		return;
	}
}

/*
 *	Device is about to be reset
 *
 *	Every participant is notified even if an earlier one misbehaves: a single
 *	outstanding default-pool reference is enough to make Reset() fail.
 */
void CRS2ResetRegistry::NotifyBeforeReset(){
	if(!m_Count) return;

	Debug("[RS2EX Resource] before reset: %d participant(s)\n", m_Count);

	int i;
	for(i = 0; i<m_Count; i++) m_Participants[i]->OnRendererBeforeReset();

	m_BeforeResetCalls++;
}

/*
 *	Device has been reset successfully
 *
 *	returns	: false if any participant could not rebuild itself
 *
 *	A failure is reported but does not stop the remaining participants; each is
 *	responsible for leaving itself safely unusable.  Only call this after a
 *	successful Reset() - a failed reset leaves no healthy device to build on.
 */
bool CRS2ResetRegistry::NotifyAfterReset(){
	if(!m_Count) return true;

	bool allRecreated = true;
	int i;

	for(i = 0; i<m_Count; i++){
		if(m_Participants[i]->OnRendererAfterReset()) continue;
		allRecreated = false;
		m_RecreateFailures++;
	}
	m_AfterResetCalls++;

	Debug("[RS2EX Resource] after reset: %d participant(s), %s\n",
		m_Count, allRecreated ? "all recreated" : "RECREATION FAILED");

	return allRecreated;
}
