//	Modified for RS2EX on 2026-09-20, 2026-09-21, 2026-09-26.
#include "stdafx.h"
#include "CPixelbit.h"
#include "Capture.h"
#include "RS2Renderer.h"
#include "CCamera.h"
#include "CSkinPlugin.h"
#include "CVideoMode.h"
#include "CConfigMode.h"
#include "CSceneryMode.h"
#include "CSaveFile.h"

#include <vfw.h>
#pragma comment ( lib, "vfw32.lib" )

//	内部グローバル
int g_VideoState = 0;				//	撮影状態
int g_VideoFrame = 0;				//	連番BMPフレーム番号
int g_VideoCount = 0;				//	AVIファイルカウント
int g_VideoAVIFrame = 0;
int g_PictureCount = 0;				//	スクリーンショットカウント
CPixelbit g_ScreenShot;				//	スクリーンショットバッファ
CPixelbit g_SmallVideo;				//	ダウンサンプルバッファ
CPixelbit g_Video24bit;				//	24bppバッファ
COffScreen g_HidefCapture;			//	高解像度撮影用バッファ
bool g_HidefCaptureFlag = false;	//	高解像度撮影フラグ
int g_HidefBufferSize = 512;		//	バッファサイズ
int g_HidefQuality = 4;				//	拡大倍率
float g_HidefLeft, g_HidefRight;	//	水平クリップ
float g_HidefBottom, g_HidefTop;	//	垂直クリップ
int g_DownsampleMode = 0;			//	ダウンサンプルモード
int g_DownsampleWidth = 0;			//	ビデオ幅
int g_DownsampleHeight = 0;			//	ビデオ高さ
int g_VideoFormat = 0;				//	ビデオフォーマット
bool g_VideoSound = true;			//	録音

PAVIFILE g_AVI;
PAVISTREAM g_VideoStream;
PAVISTREAM g_AudioStream;

/*
 *	撮影初期化
 */
void InitCapture(){
	AVIFileInit();
	g_VideoState = 0;
	g_VideoFrame = 0;
	g_VideoCount = 0;
	g_PictureCount = 0;
//	g_HidefCapture.Create(g_HidefBufferSize, g_HidefBufferSize);
	g_ScreenShot.Clear(g_DispWidth, g_DispHeight);
	g_HidefBufferSize = CheckArguments("-voodoo") ? 256 : 512;
	chdir(g_BaseDir);
	if(chdir("Picture")) mkdir("Picture");
	chdir(g_BaseDir);
	if(chdir("Video")) mkdir("Video");
	CountPicture();
	CountVideoAVI();
	chdir(g_BaseDir);
}
/*
 *	撮影解放
 */
void ReleaseCaptureRS(){
	AVIFileExit( );
}

/*
 *	高画質撮影
 */
void HidefCapture(CSceneryMode *scenerymode){
	//	[RS2EX] Readback is a deferred capability (v0.0.9 WP9), and the
	//	Direct3D 8 readback that was here went with the Direct3D 8 renderer in
	//	v0.2.0.  Capture refresh is v0.4.0 scope.
	(void)scenerymode;
	if(!GetRS2Renderer().SupportsReadback()) return;
}

/*
 *	高速縮小処理1
 */
bool FastDownSample(
	CPixelbit* dst,	//	格納先
	CPixelbit* src,	//	縮小元
	int shift		//	シフト値 (1=>1/2, 2=>1/4)
){
	if(shift<=0) return false;
	const int sw = src->GetWidth(), sh = src->GetHeight();
	if(sw&((1<<shift)-1) || sh&((1<<shift)-1)) return false;
	const int dw = sw>>shift, dh = sh>>shift;
	if(sw<=0 || sh<=0) return false;
	if(dst->GetWidth()!=dw || dst->GetHeight()!=dh) dst->Clear(dw, dh);
	int dx, dy, /*sx,*/ sy;
	if(shift==1){
		for(dy = sy = 0; dy<dh; ++dy, sy += 2){
			PDWORD dp = dst->GetScanLine(dy);
			PDWORD sp1 = src->GetScanLine(sy);
			PDWORD sp2 = src->GetScanLine(sy+1);
			for(dx = 0; dx<dw; ++dx){
				DWORD v1 = sp1[0], v2 = sp1[1], v3 = sp2[0], v4 = sp2[1];
				*dp = (((v1&0x00fcfcfc)+(v2&0x00fcfcfc)+(v3&0x00fcfcfc)+(v4&0x00fcfcfc))>>2)
					+((((v1&0x00030303)+(v2&0x00030303)+(v3&0x00030303)+(v4&0x00030303))>>2)&0x00030303);
				++dp; sp1 += 2; sp2 += 2;
			}
		}
	}else if(shift==2){
		for(dy = sy = 0; dy<dh; ++dy, sy += 4){
			PDWORD dp = dst->GetScanLine(dy);
			PDWORD sp1 = src->GetScanLine(sy+0);
			PDWORD sp2 = src->GetScanLine(sy+1);
			PDWORD sp3 = src->GetScanLine(sy+2);
			PDWORD sp4 = src->GetScanLine(sy+3);
			for(dx = 0; dx<dw; ++dx){
				DWORD v01 = sp1[0], v02 = sp1[1], v03 = sp1[2], v04 = sp1[3];
				DWORD v05 = sp2[0], v06 = sp2[1], v07 = sp2[2], v08 = sp2[3];
				DWORD v09 = sp3[0], v10 = sp3[1], v11 = sp3[2], v12 = sp3[3];
				DWORD v13 = sp4[0], v14 = sp4[1], v15 = sp4[2], v16 = sp4[3];
				*dp = (((v01&0x00f0f0f0)+(v02&0x00f0f0f0)+(v03&0x00f0f0f0)+(v04&0x00f0f0f0)
					+(v05&0x00f0f0f0)+(v06&0x00f0f0f0)+(v07&0x00f0f0f0)+(v08&0x00f0f0f0)
					+(v09&0x00f0f0f0)+(v10&0x00f0f0f0)+(v11&0x00f0f0f0)+(v12&0x00f0f0f0)
					+(v13&0x00f0f0f0)+(v14&0x00f0f0f0)+(v15&0x00f0f0f0)+(v16&0x00f0f0f0))>>4)
					+((((v01&0x000f0f0f)+(v02&0x000f0f0f)+(v03&0x000f0f0f)+(v04&0x000f0f0f)
					+(v05&0x000f0f0f)+(v06&0x000f0f0f)+(v07&0x000f0f0f)+(v08&0x000f0f0f)
					+(v09&0x000f0f0f)+(v10&0x000f0f0f)+(v11&0x000f0f0f)+(v12&0x000f0f0f)
					+(v13&0x000f0f0f)+(v14&0x000f0f0f)+(v15&0x000f0f0f)+(v16&0x000f0f0f))>>4)&0x000f0f0f);
				++dp; sp1 += 4; sp2 += 4; sp3 += 4; sp4 += 4;
			}
		}
	}else{
		return false; // not supported yet
	}
	return true;
}

/*
 *	ビデオの撮影
 */
void VideoCapture(
	int video,	//	ビデオ撮影モード (1: record, 2: paused, 4: photo mode)
	CSceneryMode *scenerymode	//	シーナリモード
){
	if(GetKey(DIK_F12)==S_PUSH){
		//	[RS2EX] v0.2.0: Direct3D 12 cannot read the picture back yet
		//	(capture refresh is v0.4.0 scope), and with the Direct3D 8
		//	renderer gone there is no backend that can.  A GDI copy of a
		//	flip-model window is black, and the high-quality path does
		//	nothing, so say so instead of writing black files or nothing at
		//	all.  Stopping a video that is somehow running still works.
		if(!GetRS2Renderer().SupportsReadback() && !(g_VideoState && CheckCtrl() && CheckShift())){
			Debug("[RS2EX] capture is not available on this renderer (no readback)\n");
			g_Skin->Error();
		}else if(g_RSPV || !CheckCtrl()){
			chdir(g_BaseDir);
			chdir("Picture");
			g_HidefQuality = g_VideoMode->GetPictureQuality();
			if(g_HidefQuality>1 && scenerymode && !g_ConfigMode->GetStereo()){
				HidefCapture(scenerymode);
			}else{
				HDC windc = GetDC(svw.hWnd);
				BitBlt(g_ScreenShot.GetHDC(), 0, 0, g_DispWidth, g_DispHeight, windc, 0, 0, SRCCOPY);
				ReleaseDC(svw.hWnd, windc);
				g_ScreenShot.Save(FlashIn("%08d.bmp", g_PictureCount), 24);
				CountPicture();
				g_Skin->ScreenShot();
			}
		}else if(CheckShift()){
			StopVideoCapture();
		}else{
			StartVideoCapture();
		}
	}
	if(!g_RSPV && g_VideoState && (video&1)
		&& (!(video&2) || !g_VideoMode->GetExceptPause())
		&& ((video&4) || !g_VideoMode->GetOnlyPhotoMode())){
		HDC windc = GetDC(svw.hWnd);
		BitBlt(g_ScreenShot.GetHDC(), 0, 0, g_DispWidth, g_DispHeight, windc, 0, 0, SRCCOPY);
		ReleaseDC(svw.hWnd, windc);
		CPixelbit* saving_img = &g_ScreenShot;
		if(g_DownsampleMode){
			FastDownSample(&g_SmallVideo, &g_ScreenShot, g_DownsampleMode);
			saving_img = &g_SmallVideo;
		}
		if(g_VideoFormat==1){
			if(!g_Video24bit.CheckSize(g_DownsampleWidth, g_DownsampleHeight))
				g_Video24bit.Clear(g_DownsampleWidth, /*must be bottom-up*/-g_DownsampleHeight, 24);
			saving_img->PlainStamp(&g_Video24bit, 0, 0, 0, 0, g_DownsampleWidth, g_DownsampleHeight);
			LPBITMAPINFOHEADER pbmih = reinterpret_cast< LPBITMAPINFOHEADER >( g_Video24bit.GetBmpInfo( ) );
			int bmp_size = g_Video24bit.GetBytesPerLine( ) * g_DownsampleHeight;
			if ( AVIStreamSetFormat( g_VideoStream, g_VideoAVIFrame, pbmih, sizeof( BITMAPINFOHEADER ) ) != 0 )
			{
				Dialog( "Video stream set format failed." );
			}
			if ( AVIStreamWrite( g_VideoStream, g_VideoAVIFrame, 1, g_Video24bit.GetScanLine( 0 ),
				bmp_size, AVIIF_KEYFRAME, NULL, NULL ) != 0 )
			{
				Dialog( "Video stream write failed." );
			}
			++g_VideoAVIFrame;
		}else{
			chdir(g_BaseDir);
			chdir("Video");
			char* frame_name = FlashIn("%08d.bmp", g_VideoFrame);
				saving_img->Save(frame_name, 24);
		}
		g_VideoFrame++;
	}
}

/*
 *	スクリーンショットの既存ファイルのカウント
 */
void CountPicture(){
	FILE *file;
	chdir(g_BaseDir);
	chdir("Picture");
	while(true){
		if(file = fopen(FlashIn("%08d.bmp", g_PictureCount), "rb")){
			fclose(file);
			g_PictureCount++;
		}else{
			break;
		}
	}
}

/*
 *	ビデオBMPの既存ファイルのカウント
 */
void CountVideoBMP(){
	FILE *file;
	chdir(g_BaseDir);
	chdir("Video");
	while(true){
		if(file = fopen(FlashIn("%08d.bmp", g_VideoFrame), "rb")){
			fclose(file);
			g_VideoFrame++;
		}else{
			break;
		}
	}
}

/*
 *	ビデオAVIの既存ファイルのカウント
 */
void CountVideoAVI(){
	FILE *file;
	chdir(g_BaseDir);
	chdir("Video");
	while(true){
		if(file = fopen(FlashIn("%08d.avi", g_VideoCount), "rb")){
			fclose(file);
			g_VideoCount++;
		}else{
			break;
		}
	}
}

/*
 *	ビデオ撮影開始
 */
void StartVideoCapture(){
	if(g_VideoState) return;
	//	[RS2EX] v0.2.0: no readback, no video - see VideoCapture().
	if(!GetRS2Renderer().SupportsReadback()){
		Debug("[RS2EX] video capture is not available on this renderer (no readback)\n");
		g_Skin->Error();
		return;
	}
	g_DownsampleMode = g_VideoMode->GetDownsample();
	int exp_ds = 1<<g_DownsampleMode;
	if(g_DispWidth%exp_ds || g_DispHeight%exp_ds)
	{
		g_DownsampleMode = 0;
		exp_ds = 1;
	}
	g_DownsampleWidth = g_DispWidth/exp_ds;
	g_DownsampleHeight = g_DispHeight/exp_ds;
	g_VideoFormat = g_VideoMode->GetFormat();
	g_VideoSound = false;//!!g_VideoMode->GetVideoSound();
	if(g_VideoFormat==1){
		g_VideoAVIFrame = 0;
		chdir(g_BaseDir);
		chdir("Video");
		std::string avifile_name = FlashIn("%08d.avi", g_VideoCount);
		//	[RS2EX] VideoCapture() writes one frame per rendered frame, so the AVI
		//	rate has to follow the render target.  Leaving this at 30 while
		//	rendering at 60 would store 60 frames for every second of real time
		//	and play them back at 30, i.e. at half speed.
		const int video_frame_per_sec = RENDER_TARGET_FPS;

#if 0
		WAVEFORMATEX wave_format;
		LPPCMWAVEFORMAT pcm_wave_format = reinterpret_cast< LPPCMWAVEFORMAT >( &wave_format );
		wave_format.wFormatTag = WAVE_FORMAT_PCM;
		wave_format.nChannels = 1;
		wave_format.nSamplesPerSec = 22050;
		wave_format.wBitsPerSample = 8;
		wave_format.nBlockAlign = wave_format.nChannels * wave_format.wBitsPerSample / 8;
		wave_format.nAvgBytesPerSec = wave_format.nSamplesPerSec * wave_format.nBlockAlign;
		wave_format.cbSize = wave_format.nAvgBytesPerSec * video_frame_count / video_frame_per_sec;
		const int audio_sample_max = wave_format.nSamplesPerSec * video_frame_count / video_frame_per_sec;
		const int audio_size = audio_sample_max;
		unsigned char* wave_data = new unsigned char[ audio_sample_max ];
		for ( i = 0; i < audio_sample_max; ++i ) wave_data[ i ] = i & 0xff;
#endif

		AVISTREAMINFO video_stream_info =
		{
			streamtypeVIDEO, // DWORD fccType;
			mmioFOURCC( 'D', 'I', 'B', ' '), // DWORD fccHandler;
			0, // DWORD dwFlags; /* Contains AVITF_* flags */
			0, // DWORD dwCaps;
			0, // WORD wPriority;
			0, // WORD wLanguage;
			1, // DWORD dwScale;
			video_frame_per_sec, // DWORD dwRate; /* dwRate / dwScale == samples/second */
			0, // DWORD dwStart;
			0/*video_frame_count*/, // DWORD dwLength; /* In units above... */
			0, // DWORD dwInitialFrames;
			0, // DWORD dwSuggestedBufferSize;
			static_cast< DWORD >( -1 ), // DWORD dwQuality;
			0, // DWORD dwSampleSize;
			{ 0, 0, g_DownsampleWidth, g_DownsampleHeight }, // RECT rcFrame;
			0, // DWORD dwEditCount;
			0, // DWORD dwFormatChangeCount;
			"my video stream" // char szName[64];
		};
#if 0
		AVISTREAMINFO audio_stream_info =
		{
			streamtypeAUDIO, // DWORD fccType;
			0, // DWORD fccHandler;
			0, // DWORD dwFlags; /* Contains AVITF_* flags */
			0, // DWORD dwCaps;
			0, // WORD wPriority;
			0, // WORD wLanguage;
			1, // DWORD dwScale;
			wave_format.nSamplesPerSec, // DWORD dwRate; /* dwRate / dwScale == samples/second */
			0, // DWORD dwStart;
			audio_sample_max, // DWORD dwLength; /* In units above... */
			0, // DWORD dwInitialFrames;
			0, // DWORD dwSuggestedBufferSize;
			0, // DWORD dwQuality;
			wave_format.nBlockAlign, // DWORD dwSampleSize;
			{ 0, 0, 0, 0 }, // RECT rcFrame;
			0, // DWORD dwEditCount;
			0, // DWORD dwFormatChangeCount;
			"my audio stream" // char szName[64];
		};
#endif
		try
		{
			if ( AVIFileOpen( &g_AVI, avifile_name.c_str( ),
				OF_CREATE | OF_WRITE | OF_SHARE_DENY_NONE, NULL) != 0 )
			{
				Dialog( "AVI file creation failed." );
				throw 0;
			}
			if ( AVIFileCreateStream( g_AVI, &g_VideoStream, &video_stream_info ) != 0 )
			{
				Dialog( "Video stream creation failed." );
				throw 1;
			}
			/*if ( AVIFileCreateStream( g_AVI, &g_AudioStream, &audio_stream_info ) != 0 )
			{
				Dialog( "Audio stream creation failed." );
				throw 2;
			}*/
		}
		catch ( int code )
		{
			switch ( code )
			{
			case 3:
				AVIStreamRelease( g_AudioStream );
			case 2:
				AVIStreamRelease( g_VideoStream );
			case 1:
				AVIFileRelease( g_AVI );
			case 0:
				g_Skin->Error();
				return;
			}
		}
		//delete [] wave_data;
	}
	g_VideoState  = 1;
	g_Skin->VideoStart();
}

/*
 *	ビデオ撮影停止
 */
void StopVideoCapture(){
	if(!g_VideoState) return;
	if(g_VideoFormat==1){
#if 0
		if ( AVIStreamSetFormat( g_AudioStream, 0, pcm_wave_format, sizeof( PCMWAVEFORMAT ) ) != 0 )
		{
			Dialog( "Audio stream set format failed." );
			throw 11;
		}
		if ( AVIStreamWrite( g_AudioStream, 0, audio_size, reinterpret_cast< LPBYTE >( wave_data ),
			audio_size, AVIIF_KEYFRAME, NULL, NULL ) != 0 )
		{
			Dialog( "Audio stream set format failed." );
			throw 11;
		}
#endif
		AVIStreamRelease( g_VideoStream );
		//AVIStreamRelease( g_AudioStream );
		AVIFileRelease( g_AVI );
		CountVideoAVI( );
	}
	g_VideoState  = 0;
	g_Skin->VideoStop();
}
