//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-20.
//
//	Renderer reset participation.
//
//	Direct3D 8 destroys every D3DPOOL_DEFAULT resource when the device is reset,
//	and refuses to reset at all while a swap-chain surface reference is held.
//	RailSim II 2.15 had no mechanism for this: the only default-pool owner in the
//	program, COffScreen, simply kept its render target across a reset.
//
//	This header is deliberately API-neutral - no Direct3D type appears in it - so
//	the notion of "a resource that must be released before the device is reset and
//	rebuilt afterwards" survives a change of backend.

#ifndef RS2RENDERRESOURCE_H_INCLUDED
#define RS2RENDERRESOURCE_H_INCLUDED

/*
 *	A GPU resource whose lifetime is tied to the device.
 *
 *	Implementors register themselves while they hold device-dependent resources
 *	and unregister before they are destroyed.  Registration is not ownership: the
 *	registry never deletes a participant.
 */
class IRS2RendererResetParticipant
{
public:
	virtual ~IRS2RendererResetParticipant(){}

	//	Release everything the device is about to invalidate.  Must leave no
	//	surface or texture reference outstanding, including references borrowed
	//	from the swap chain, or the reset itself will fail.
	virtual void OnRendererBeforeReset() = 0;

	//	Rebuild what was released.  Returns false if the resource could not be
	//	recreated; the participant must then be left safely unusable rather than
	//	holding a partial state.
	virtual bool OnRendererAfterReset() = 0;
};

/*
 *	Registry of reset participants.
 *
 *	Ordinary destruction and device reset are separate events and must not be
 *	confused: destroying a participant unregisters it, it does not notify it.
 */
class CRS2ResetRegistry
{
public:
	enum { MAX_PARTICIPANTS = 16 };

private:
	IRS2RendererResetParticipant *m_Participants[MAX_PARTICIPANTS];
	int m_Count;

	//	Diagnostics, for validation rather than gameplay.
	unsigned int m_BeforeResetCalls;
	unsigned int m_AfterResetCalls;
	unsigned int m_RecreateFailures;

public:
	CRS2ResetRegistry();

	//	Both are safe to call twice and safe to call with a participant that was
	//	never registered.
	void Register(IRS2RendererResetParticipant *participant);
	void Unregister(IRS2RendererResetParticipant *participant);

	void NotifyBeforeReset();
	bool NotifyAfterReset();

	int GetParticipantCount() const{ return m_Count; }
	unsigned int GetBeforeResetCalls() const{ return m_BeforeResetCalls; }
	unsigned int GetAfterResetCalls() const{ return m_AfterResetCalls; }
	unsigned int GetRecreateFailures() const{ return m_RecreateFailures; }
};

/*
 *	The registry instance.
 *
 *	Deliberately never deleted, for the same reason GetRS2Renderer() is not: the
 *	resource owners that register here include file-scope globals (g_TexList,
 *	g_HidefCapture), and a registry destroyed partway through static teardown
 *	would be called by whichever of them happens to be destroyed after it.
 */
CRS2ResetRegistry &GetRS2ResetRegistry();

#endif	//	RS2RENDERRESOURCE_H_INCLUDED
