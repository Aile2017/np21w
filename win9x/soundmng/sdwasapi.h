/**
 * @file	sdwasapi.h
 * @brief	WASAPI オーディオ クラスの宣言およびインターフェイスの定義をします
 */

#pragma once

#include <vector>
#include <string>
#include <mmreg.h>
#include "sdbase.h"
#include "misc\threadbase.h"

class CSoundDeviceDSound3;
struct IMMDeviceEnumerator;
struct IMMNotificationClient;
struct IMMDevice;
struct IAudioClient;
struct IAudioSessionControl;
struct IAudioSessionEvents;
struct IAudioRenderClient;

// The UI thread handles this message after the notification callback returns.
#define WM_NP2W_WASAPI_REOPEN (WM_APP + 0x438)

/**
 * @brief デバイス
 */
struct WasapiDevice
{
	LPWSTR id;							//!< ID
	TCHAR szDevice[MAX_PATH];			//!< デバイス
};

/**
 * @brief WASAPI クラス
 */
class CSoundDeviceWasapi : public CSoundDeviceBase, protected CThreadBase
{
public:
	static void Initialize();
	static void Deinitialize();
	static void EnumerateDevices(std::vector<LPCTSTR>& devices);
	static void CompleteReopenRequest();

	explicit CSoundDeviceWasapi(bool bShared = false);
	virtual ~CSoundDeviceWasapi();
	virtual bool Open(LPCTSTR lpDevice = NULL, HWND hWnd = NULL);
	bool Reopen();
	virtual UINT GetSamplingRate(UINT nRequestedRate) const;
	virtual void Close();
	virtual UINT CreateStream(UINT nSamplingRate, UINT nChannels, UINT nBufferSize = 0);
	virtual void DestroyStream();
	virtual bool PlayStream();
	virtual void StopStream();
	virtual void SetMasterVolume(int nVolume);
	virtual bool LoadPCM(UINT nNum, LPCTSTR lpFilename = NULL);
	virtual bool ReloadPCM(UINT nNum);
	virtual void SetPCMVolume(UINT nNum, int nVolume);
	virtual bool PlayPCM(UINT nNum, BOOL bLoop);
	virtual void StopPCM(UINT nNum);
	virtual void StopAllPCM();

protected:
	virtual bool Task();

private:
	static std::vector<WasapiDevice> sm_devices;	//!< デバイス リスト
	CSoundDeviceDSound3* m_pPCMDevice;	//!< DirectSound effects in shared mode only
	bool m_bShared;						//!< 共有モード
	bool m_bDefaultDevice;				//!< 既定の出力デバイスを使用
	HWND m_hWnd;
	std::wstring m_deviceId;			//!< 明示指定されたデバイスID
	UINT m_nMixSamplingRate;			//!< システム ミックス レート
	int m_mastervolume;				//!< マスタボリューム

	IMMDeviceEnumerator* m_pEnumerator;			//!< デバイス列挙インスタンス
	IMMNotificationClient* m_pDeviceNotification;
	IMMDevice* m_pDevice;						//!< デバイス インスタンス
	IAudioClient* m_pAudioClient;				//!< オーディオ クライアント インスタンス
	IAudioSessionControl* m_pSessionControl;
	IAudioSessionEvents* m_pSessionNotification;
	WAVEFORMATEX* m_pwfx;						//!< フォーマット
	IAudioRenderClient* m_pRenderClient;		//!< オーディオ レンダラー クライアント インスタンス
	UINT32 m_nBufferSize;						//!< バッファ サイズ
	std::vector<SINT16> m_sharedPCM;			//!< 共有モード用の固定長PCMバッファ
	UINT32 m_nSharedOffset;					//!< 次に出力するフレーム位置
	HANDLE m_hEvents[2];						//!< イベント
	bool RefreshEndpoint();
	void OpenPCMDevice();
	void QueueReopen();
	void ResetStream();
	void FillStream();
};
