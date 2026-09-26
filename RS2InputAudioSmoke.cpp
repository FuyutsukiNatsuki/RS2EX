//	RS2EX - RailSim II development fork
//	Created for RS2EX on 2026-09-26.
//
//	-inputaudiosmoke: the inherited DirectInput / DirectSound paths, as they
//	run (v0.3.0 WP4).
//
//	v0.3.0 does not redesign input or audio; it has to show that the existing
//	paths still initialise and work in a 64-bit process.  They log only
//	failures, so this records what they set up, after the game has started:
//
//	    DirectInput  the interface, the keyboard and the mouse (their state
//	                 can be read), the joysticks the enumeration found, and
//	                 the polling thread (running, and counting polls);
//	    DirectSound  the interface, the primary buffer and its format, the 3D
//	                 listener, the lost-buffer check, and a wave from the skin
//	                 loaded, played, reported as playing and stopped.
//
//	The same log from the Win32 and the x64 build of the same source is the
//	A/B comparison.  Shutdown (FreeInput / FreeDirectSound) is checked from
//	the normal exit that follows.

#include "stdafx.h"
#include "RS2InputAudioSmoke.h"

extern int g_InputPollCount;
extern CCrtThread g_InputPollingThread;

bool RS2InputAudioSmokeRequested(){
	return CheckArguments("-inputaudiosmoke")!=FALSE;
}

static void RS2IAStep(const char *name, bool ok, bool *all){
	Debug("RS2INPUTAUDIO|%-40s|%s\n", name, ok ? "pass" : "FAIL");
	if(!ok) *all = false;
}

bool RS2InputAudioSmokeRun(){
	bool all = true;

	Debug("RS2INPUTAUDIO|process: %d-bit\n", (int)(sizeof(void *)*8));

	//	---- DirectInput
	RS2IAStep("DirectInput8 interface", svi.pDI!=NULL, &all);
	{
		BYTE keys[256];
		HRESULT hr = svi.pKey ? svi.pKey->Acquire() : E_FAIL;
		if(svi.pKey) hr = svi.pKey->GetDeviceState(sizeof(keys), keys);
		Debug("RS2INPUTAUDIO|keyboard GetDeviceState 0x%08lx\n", (unsigned long)hr);
		RS2IAStep("keyboard device, state readable", svi.pKey && SUCCEEDED(hr), &all);
	}
	{
		DIMOUSESTATE ms;
		HRESULT hr = svi.pMouse ? svi.pMouse->Acquire() : E_FAIL;
		if(svi.pMouse) hr = svi.pMouse->GetDeviceState(sizeof(ms), &ms);
		Debug("RS2INPUTAUDIO|mouse GetDeviceState 0x%08lx\n", (unsigned long)hr);
		RS2IAStep("mouse device, state readable", svi.pMouse && SUCCEEDED(hr), &all);
	}
	{
		int created = 0, i;
		for(i = 0; i<svi.numJoy && i<MAX_JOYSTICK; i++) if(svi.pJoy[i]) created++;
		Debug("RS2INPUTAUDIO|joysticks: enabled=%d enumerated=%d created=%d\n", svi.fJoy ? 1 : 0, svi.numJoy, created);
		RS2IAStep("joystick enumeration consistent", 0<=svi.numJoy && svi.numJoy<=MAX_JOYSTICK && created==svi.numJoy, &all);
	}
	{
		HANDLE thread = g_InputPollingThread.getHandle();
		const bool running = thread && WaitForSingleObject(thread, 0)==WAIT_TIMEOUT;
		const int before = g_InputPollCount;
		Sleep(250);
		const int after = g_InputPollCount;
		Debug("RS2INPUTAUDIO|polling thread: running=%d polls in 250 ms=%d\n", running ? 1 : 0, after-before);
		RS2IAStep("polling thread running", running, &all);
		RS2IAStep("polling thread polls", after>before, &all);
	}

	//	---- DirectSound
	RS2IAStep("DirectSound8 interface", svs.pDS!=NULL, &all);
	{
		WAVEFORMATEX fmt;
		DWORD got = 0;
		ZeroMemory(&fmt, sizeof(fmt));
		const HRESULT hr = svs.pPB ? svs.pPB->GetFormat(&fmt, sizeof(fmt), &got) : E_FAIL;
		Debug("RS2INPUTAUDIO|primary buffer: 0x%08lx, %lu Hz, %u bit, %u ch\n",
			(unsigned long)hr, (unsigned long)fmt.nSamplesPerSec, (unsigned int)fmt.wBitsPerSample, (unsigned int)fmt.nChannels);
		RS2IAStep("primary buffer and format", svs.pPB && SUCCEEDED(hr) && fmt.nSamplesPerSec>0, &all);
	}
	{
		D3DVECTOR pos;
		const HRESULT hr = svs.pListener ? svs.pListener->GetPosition(&pos) : E_FAIL;
		Debug("RS2INPUTAUDIO|3D: enabled=%d listener=%d GetPosition 0x%08lx\n",
			svs.f3D ? 1 : 0, svs.pListener ? 1 : 0, (unsigned long)hr);
		RS2IAStep("3D listener (when 3D is enabled)", !svs.f3D || (svs.pListener && SUCCEEDED(hr)), &all);
	}
	PrimaryBufferVerify();
	RS2IAStep("primary buffer verify (restore path)", svs.pPB!=NULL, &all);
	{
		chdir(g_BaseDir);
		CWave wave;
		const bool loaded = wave.Load("Skin\\Default_Blue\\Error.wav")!=FALSE;
		Debug("\n");
		bool playing = false;
		if(loaded){
			wave.SetVolume(DSBVOLUME_MIN);	//	silent: the buffer plays, nobody hears it
			wave.Play();
			playing = wave.GetStatus()!=FALSE;
			Sleep(50);
			wave.Stop();
			wave.Free();
		}
		RS2IAStep("wave load (Skin\\Default_Blue\\Error.wav)", loaded, &all);
		RS2IAStep("wave plays", playing, &all);
	}

	Debug("RS2INPUTAUDIO|%s\n", all ? "pass" : "FAIL");
	return all;
}
