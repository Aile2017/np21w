/**
 * @file	sdwasapi.cpp
 * @brief	WASAPI オーディオ クラスの動作の定義を行います
 */

#include "compiler.h"
#include "sdwasapi.h"
#include "sddsound3.h"
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include <AudioPolicy.h>
#include <atlbase.h>
#include <Functiondiscoverykeys_devpkey.h>

//! デバイス リスト
std::vector<WasapiDevice> CSoundDeviceWasapi::sm_devices;

//! MMDeviceEnumerator
const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);

//! IMMDeviceEnumerator
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);

//! IAudioClient
const IID IID_IAudioClient = __uuidof(IAudioClient);

//! IAudioRenderClient
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

namespace
{
	volatile LONG s_reopenQueued = 0;

	void NotifyWasapiReopen(HWND hWnd)
	{
		if (hWnd != NULL && ::InterlockedCompareExchange(&s_reopenQueued, 1, 0) == 0)
		{
			if (!::PostMessage(hWnd, WM_NP2W_WASAPI_REOPEN, 0, 0))
			{
				::InterlockedExchange(&s_reopenQueued, 0);
			}
		}
	}

	class CWasapiDeviceNotification : public IMMNotificationClient
	{
	public:
		CWasapiDeviceNotification(HWND hWnd, bool bDefault, const std::wstring& deviceId)
			: m_refs(1), m_hWnd(hWnd), m_bDefault(bDefault), m_deviceId(deviceId) {}

		STDMETHODIMP QueryInterface(REFIID iid, void** ppv)
		{
			if (ppv == NULL) return E_POINTER;
			*ppv = NULL;
			if (iid == IID_IUnknown || iid == __uuidof(IMMNotificationClient))
			{
				*ppv = static_cast<IMMNotificationClient*>(this);
				AddRef();
				return S_OK;
			}
			return E_NOINTERFACE;
		}
		STDMETHODIMP_(ULONG) AddRef() { return static_cast<ULONG>(::InterlockedIncrement(&m_refs)); }
		STDMETHODIMP_(ULONG) Release()
		{
			const ULONG refs = static_cast<ULONG>(::InterlockedDecrement(&m_refs));
			if (refs == 0) delete this;
			return refs;
		}
		STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR)
		{
			if (m_bDefault && flow == eRender && role == eMultimedia)
			{
				NotifyWasapiReopen(m_hWnd);
			}
			return S_OK;
		}
		STDMETHODIMP OnDeviceAdded(LPCWSTR id) { NotifyExplicitDevice(id); return S_OK; }
		STDMETHODIMP OnDeviceRemoved(LPCWSTR id) { NotifyExplicitDevice(id); return S_OK; }
		STDMETHODIMP OnDeviceStateChanged(LPCWSTR id, DWORD) { NotifyExplicitDevice(id); return S_OK; }
		STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) { return S_OK; }

	private:
		void NotifyExplicitDevice(LPCWSTR id)
		{
			if (!m_bDefault && id != NULL && !m_deviceId.empty() &&
				::lstrcmpW(id, m_deviceId.c_str()) == 0)
			{
				NotifyWasapiReopen(m_hWnd);
			}
		}
		volatile LONG m_refs;
		HWND m_hWnd;
		bool m_bDefault;
		std::wstring m_deviceId;
	};

	class CWasapiSessionNotification : public IAudioSessionEvents
	{
	public:
		explicit CWasapiSessionNotification(HWND hWnd) : m_refs(1), m_hWnd(hWnd) {}
		STDMETHODIMP QueryInterface(REFIID iid, void** ppv)
		{
			if (ppv == NULL) return E_POINTER;
			*ppv = NULL;
			if (iid == IID_IUnknown || iid == __uuidof(IAudioSessionEvents))
			{
				*ppv = static_cast<IAudioSessionEvents*>(this);
				AddRef();
				return S_OK;
			}
			return E_NOINTERFACE;
		}
		STDMETHODIMP_(ULONG) AddRef() { return static_cast<ULONG>(::InterlockedIncrement(&m_refs)); }
		STDMETHODIMP_(ULONG) Release()
		{
			const ULONG refs = static_cast<ULONG>(::InterlockedDecrement(&m_refs));
			if (refs == 0) delete this;
			return refs;
		}
		STDMETHODIMP OnDisplayNameChanged(LPCWSTR, LPCGUID) { return S_OK; }
		STDMETHODIMP OnIconPathChanged(LPCWSTR, LPCGUID) { return S_OK; }
		STDMETHODIMP OnSimpleVolumeChanged(float, BOOL, LPCGUID) { return S_OK; }
		STDMETHODIMP OnChannelVolumeChanged(DWORD, float[], DWORD, LPCGUID) { return S_OK; }
		STDMETHODIMP OnGroupingParamChanged(LPCGUID, LPCGUID) { return S_OK; }
		STDMETHODIMP OnStateChanged(AudioSessionState) { return S_OK; }
		STDMETHODIMP OnSessionDisconnected(AudioSessionDisconnectReason reason)
		{
			if (reason == DisconnectReasonFormatChanged ||
				reason == DisconnectReasonDeviceRemoval ||
				reason == DisconnectReasonServerShutdown ||
				reason == DisconnectReasonExclusiveModeOverride)
			{
				NotifyWasapiReopen(m_hWnd);
			}
			return S_OK;
		}

	private:
		volatile LONG m_refs;
		HWND m_hWnd;
	};
}

void CSoundDeviceWasapi::CompleteReopenRequest()
{
	::InterlockedExchange(&s_reopenQueued, 0);
}

/**
 * 初期化
 */
void CSoundDeviceWasapi::Initialize()
{
	IMMDeviceEnumerator* pEnumerator = NULL;
	if (SUCCEEDED(::CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, reinterpret_cast<PVOID*>(&pEnumerator))))
	{
		IMMDeviceCollection* pDevices = NULL;
		if (SUCCEEDED(pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pDevices)))
		{
			UINT nNumDevices = 0;
	    	if (SUCCEEDED(pDevices->GetCount(&nNumDevices)))
			{
				for (UINT i = 0; i < nNumDevices; i++)
				{
					IMMDevice* pDevice = NULL;
					if (FAILED(pDevices->Item(i, &pDevice)))
					{
						continue;
					}

					WasapiDevice dev;
					ZeroMemory(&dev, sizeof(dev));
					if (FAILED(pDevice->GetId(&dev.id)))
					{
						pDevice->Release();
						continue;
					}

					IPropertyStore* pPropStore = NULL;
					if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pPropStore)))
					{
						PROPVARIANT varName;
						::PropVariantInit(&varName);
						pPropStore->GetValue(PKEY_Device_FriendlyName, &varName);

						USES_CONVERSION;
						::lstrcpyn(dev.szDevice, W2CT(varName.pwszVal), _countof(dev.szDevice));
						::PropVariantClear(&varName);
						pPropStore->Release();
					}
					pDevice->Release();

					sm_devices.push_back(dev);
				}
			}
			pDevices->Release();
		}
		pEnumerator->Release();
	}
}

/**
 * 解放
 */
void CSoundDeviceWasapi::Deinitialize()
{
	if (!sm_devices.empty())
	{
		for (std::vector<WasapiDevice>::iterator it = sm_devices.begin(); it != sm_devices.end(); ++it)
		{
			::CoTaskMemFree(it->id);
		}
		sm_devices.clear();
	}
}

/**
 * 列挙
 * @param[out] devices デバイス リスト
 */
void CSoundDeviceWasapi::EnumerateDevices(std::vector<LPCTSTR>& devices)
{
	for (std::vector<WasapiDevice>::const_iterator it = sm_devices.begin(); it != sm_devices.end(); ++it)
	{
		devices.push_back(it->szDevice);
	}
}

/**
 * コンストラクタ
 */
CSoundDeviceWasapi::CSoundDeviceWasapi(bool bShared)
	: m_pPCMDevice(NULL)
	, m_bShared(bShared)
	, m_bDefaultDevice(true)
	, m_hWnd(NULL)
	, m_nMixSamplingRate(0)
	, m_pEnumerator(NULL)
	, m_pDeviceNotification(NULL)
	, m_pDevice(NULL)
	, m_pAudioClient(NULL)
	, m_pSessionControl(NULL)
	, m_pSessionNotification(NULL)
	, m_pwfx(NULL)
	, m_pRenderClient(NULL)
	, m_nBufferSize(0)
	, m_nSharedOffset(0)
	, m_mastervolume(100)
{
	ZeroMemory(m_hEvents, sizeof(m_hEvents));
}

/**
 * デストラクタ
 */
CSoundDeviceWasapi::~CSoundDeviceWasapi()
{
	Close();
}

/**
 * オープン
 * @param[in] lpDevice デバイス名
 * @param[in] hWnd ウィンドウ ハンドル
 * @retval true 成功
 * @retval false 失敗
 */
bool CSoundDeviceWasapi::Open(LPCTSTR lpDevice, HWND hWnd)
{
	m_hWnd = hWnd;
	m_bDefaultDevice = (lpDevice == NULL || lpDevice[0] == 0);
	m_deviceId.clear();
	if (!m_bDefaultDevice)
	{
		for (std::vector<WasapiDevice>::const_iterator it = sm_devices.begin(); it != sm_devices.end(); ++it)
		{
			if (::lstrcmpi(lpDevice, it->szDevice) == 0)
			{
				m_deviceId = it->id;
				break;
			}
		}
		if (m_deviceId.empty()) return false;
	}
	if (FAILED(::CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, reinterpret_cast<PVOID*>(&m_pEnumerator))))
	{
		return false;
	}
	m_pDeviceNotification = new CWasapiDeviceNotification(hWnd, m_bDefaultDevice, m_deviceId);
	if (FAILED(m_pEnumerator->RegisterEndpointNotificationCallback(m_pDeviceNotification)))
	{
		m_pDeviceNotification->Release();
		m_pDeviceNotification = NULL;
	}
	if (!RefreshEndpoint())
	{
		Close();
		return false;
	}
	return true;
}

bool CSoundDeviceWasapi::RefreshEndpoint()
{
	delete m_pPCMDevice;
	m_pPCMDevice = NULL;
	if (m_pDevice != NULL)
	{
		m_pDevice->Release();
		m_pDevice = NULL;
	}
	m_nMixSamplingRate = 0;
	if (m_pEnumerator == NULL) return false;
	HRESULT hr = m_bDefaultDevice ?
		m_pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &m_pDevice) :
		m_pEnumerator->GetDevice(m_deviceId.c_str(), &m_pDevice);
	if (FAILED(hr))
	{
		return false;
	}
	if (m_bShared)
	{
		IAudioClient* pClient = NULL;
		WAVEFORMATEX* pMixFormat = NULL;
		if (SUCCEEDED(m_pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, reinterpret_cast<PVOID*>(&pClient))))
		{
			if (SUCCEEDED(pClient->GetMixFormat(&pMixFormat)) && (pMixFormat != NULL))
			{
				m_nMixSamplingRate = pMixFormat->nSamplesPerSec;
			}
			pClient->Release();
		}
		if (pMixFormat != NULL)
		{
			::CoTaskMemFree(pMixFormat);
		}
		if ((m_nMixSamplingRate < 8000) || (m_nMixSamplingRate > 384000))
		{
			m_pDevice->Release();
			m_pDevice = NULL;
			return false;
		}
		OpenPCMDevice();
	}
	return true;
}

// Reuse ordinary DirectSound device selection for operation sounds.
void CSoundDeviceWasapi::OpenPCMDevice()
{
	LPCTSTR lpDevice = NULL;
	if (!m_bDefaultDevice)
	{
		for (std::vector<WasapiDevice>::const_iterator it = sm_devices.begin(); it != sm_devices.end(); ++it)
		{
			if (m_deviceId == it->id)
			{
				lpDevice = it->szDevice;
				break;
			}
		}
		// Do not silently route an explicitly selected device to the default output.
		if (lpDevice == NULL || lpDevice[0] == 0)
		{
			return;
		}
	}

	CSoundDeviceDSound3* device = new CSoundDeviceDSound3;
	if (device->Open(lpDevice, m_hWnd))
	{
		device->SetMasterVolume(m_mastervolume);
		m_pPCMDevice = device;
	}
	else
	{
		delete device;
	}
}

bool CSoundDeviceWasapi::Reopen()
{
	DestroyStream();
	return RefreshEndpoint();
}

void CSoundDeviceWasapi::QueueReopen()
{
	NotifyWasapiReopen(m_hWnd);
}

UINT CSoundDeviceWasapi::GetSamplingRate(UINT nRequestedRate) const
{
	return m_bShared ? m_nMixSamplingRate : nRequestedRate;
}

/**
 * クローズ
 */
void CSoundDeviceWasapi::Close()
{
	DestroyStream();
	delete m_pPCMDevice;
	m_pPCMDevice = NULL;
	if (m_pEnumerator != NULL && m_pDeviceNotification != NULL)
	{
		m_pEnumerator->UnregisterEndpointNotificationCallback(m_pDeviceNotification);
	}
	if (m_pDeviceNotification != NULL)
	{
		m_pDeviceNotification->Release();
		m_pDeviceNotification = NULL;
	}

	if (m_pDevice)
	{
		m_pDevice->Release();
		m_pDevice = NULL;
	}
	if (m_pEnumerator)
	{
		m_pEnumerator->Release();
		m_pEnumerator = NULL;
	}
	m_nMixSamplingRate = 0;
	m_hWnd = NULL;
	m_deviceId.clear();
}

/**
 * ストリームの作成
 * @param[in] nSamplingRate サンプリング レート
 * @param[in] nChannels チャネル数
 * @param[in] nBufferSize バッファ サイズ
 * @return バッファ サイズ
 */
UINT CSoundDeviceWasapi::CreateStream(UINT nSamplingRate, UINT nChannels, UINT nBufferSize)
{
	do
	{
		if (m_pDevice == NULL)
		{
			break;
		}
		if (m_bShared && (nSamplingRate != m_nMixSamplingRate))
		{
			break;
		}

		if (FAILED(m_pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, reinterpret_cast<PVOID*>(&m_pAudioClient))))
		{
			break;
		}

		m_pwfx = static_cast<WAVEFORMATEX*>(::CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
		if (m_pwfx == NULL)
		{
			break;
		}

		ZeroMemory(m_pwfx, sizeof(*m_pwfx));
		m_pwfx->wFormatTag = WAVE_FORMAT_PCM;
		m_pwfx->nChannels = nChannels;
		m_pwfx->nSamplesPerSec = nSamplingRate;
		m_pwfx->nAvgBytesPerSec = nSamplingRate * nChannels * (16 / 8);
		m_pwfx->nBlockAlign = (16 / 8) * nChannels;
		m_pwfx->wBitsPerSample = 16;

		// Shared mode uses the mix rate, while the Windows audio engine
		// handles PCM16/stereo conversion to its native mix format.
		HRESULT hrFormat;
		if (m_bShared)
		{
			WAVEFORMATEX* pClosestMatch = NULL;
			hrFormat = m_pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, m_pwfx, &pClosestMatch);
			if (pClosestMatch != NULL)
			{
				::CoTaskMemFree(pClosestMatch);
			}
		}
		else
		{
			hrFormat = m_pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, m_pwfx, NULL);
		}
		if (hrFormat != S_OK)
		{
			// printf("invalid format\n");
			break;
		}

		REFERENCE_TIME hnsRequestedDuration = 0;
		if (nBufferSize == 0)
		{
			m_pAudioClient->GetDevicePeriod(&hnsRequestedDuration, NULL);
		}
		else
		{
			hnsRequestedDuration = static_cast<REFERENCE_TIME>(((nBufferSize * 10000000.0) / nSamplingRate) + 0.5);
		}
		const AUDCLNT_SHAREMODE nShareMode = m_bShared ? AUDCLNT_SHAREMODE_SHARED : AUDCLNT_SHAREMODE_EXCLUSIVE;
		HRESULT hr = m_pAudioClient->Initialize(nShareMode, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST, hnsRequestedDuration, m_bShared ? 0 : hnsRequestedDuration, m_pwfx, NULL);
		if (!m_bShared && (hr == AUDCLNT_ERR(0x019) /* AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED */))
		{
			UINT32 nCurrentBufferSize = 0;
			if (FAILED(m_pAudioClient->GetBufferSize(&nCurrentBufferSize)))
			{
				break;
			}
			hnsRequestedDuration = static_cast<REFERENCE_TIME>(((nCurrentBufferSize * 10000000.0) / nSamplingRate) + 0.5);
			hr = m_pAudioClient->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST, hnsRequestedDuration, hnsRequestedDuration, m_pwfx, NULL);
		}
		if (FAILED(hr))
		{
			printf("Failed initiaziling (%x)\n", hr);
			break;
		}

		if (FAILED(m_pAudioClient->GetBufferSize(&m_nBufferSize)))
		{
			break;
		}
		if (m_nBufferSize == 0)
		{
			break;
		}
		if (m_bShared)
		{
			// The emulation core consumes one complete block on each Get16 call.
			// Keep that block and drain it in the smaller WASAPI packets.
			m_sharedPCM.resize(m_nBufferSize * 2);
			m_nSharedOffset = m_nBufferSize;
		}

		m_hEvents[0] = ::CreateEvent(NULL, FALSE, FALSE, NULL);
		if (FAILED(m_pAudioClient->SetEventHandle(m_hEvents[0])))
		{
			break;
		}

		if (FAILED(m_pAudioClient->GetService(IID_IAudioRenderClient, reinterpret_cast<PVOID*>(&m_pRenderClient))))
		{
			break;
		}
		if (SUCCEEDED(m_pAudioClient->GetService(__uuidof(IAudioSessionControl), reinterpret_cast<PVOID*>(&m_pSessionControl))))
		{
			m_pSessionNotification = new CWasapiSessionNotification(m_hWnd);
			if (FAILED(m_pSessionControl->RegisterAudioSessionNotification(m_pSessionNotification)))
			{
				m_pSessionNotification->Release();
				m_pSessionNotification = NULL;
			}
		}

		SetMasterVolume(m_mastervolume);

		ResetStream();

		m_hEvents[1] = ::CreateEvent(NULL, FALSE, FALSE, NULL);
		CThreadBase::Start(THREAD_PRIORITY_ABOVE_NORMAL);

		return m_nBufferSize;

	} while (false /*CONSTCOND*/);

	DestroyStream();
	return 0;
}

/**
 * ストリームを破棄
 */
void CSoundDeviceWasapi::DestroyStream()
{
	if (m_pSessionControl != NULL && m_pSessionNotification != NULL)
	{
		m_pSessionControl->UnregisterAudioSessionNotification(m_pSessionNotification);
	}
	if (m_pSessionNotification != NULL)
	{
		m_pSessionNotification->Release();
		m_pSessionNotification = NULL;
	}
	if (m_pSessionControl != NULL)
	{
		m_pSessionControl->Release();
		m_pSessionControl = NULL;
	}
	if (m_hEvents[1])
	{
		::SetEvent(m_hEvents[1]);
		CThreadBase::Stop();
	}

	if (m_pAudioClient)
	{
		m_pAudioClient->Stop();
		m_pAudioClient->Release();
		m_pAudioClient = NULL;
	}

	if (m_pRenderClient)
	{
		m_pRenderClient->Release();
		m_pRenderClient = NULL;
	}

	if (m_pwfx)
	{
		::CoTaskMemFree(m_pwfx);
		m_pwfx = NULL;
	}
	m_sharedPCM.clear();
	m_nSharedOffset = 0;

	for (UINT i = 0; i < _countof(m_hEvents); i++)
	{
		if (m_hEvents[i])
		{
			::CloseHandle(m_hEvents[i]);
			m_hEvents[i] = NULL;
		}
	}
}

/**
 * ストリームをリセット
 */
void CSoundDeviceWasapi::ResetStream()
{
	if (m_pRenderClient)
	{
		m_nSharedOffset = m_nBufferSize;
		// Shared streams must be stopped before the queued frames can be discarded.
		if (m_bShared && FAILED(m_pAudioClient->Reset()))
		{
			return;
		}
		// Grab the entire buffer for the initial fill operation.
		BYTE* pData = NULL;
		HRESULT hr = m_pRenderClient->GetBuffer(m_nBufferSize, &pData);
		if (SUCCEEDED(hr))
		{
			ZeroMemory(pData, m_nBufferSize * m_pwfx->nBlockAlign);
			m_pRenderClient->ReleaseBuffer(m_nBufferSize, AUDCLNT_BUFFERFLAGS_SILENT);
		}
	}
}

/**
 * ストリームの再生
 * @retval true 成功
 * @retval false 失敗
 */
bool CSoundDeviceWasapi::PlayStream()
{
	if (m_pAudioClient)
	{
		const HRESULT hr = m_pAudioClient->Start();
		if (SUCCEEDED(hr))
		{
			return true;
		}
		if (hr == AUDCLNT_E_DEVICE_INVALIDATED) QueueReopen();
	}
	return false;
}

/**
 * ストリームの停止
 */
void CSoundDeviceWasapi::StopStream()
{
	if (m_pAudioClient)
	{
		m_pAudioClient->Stop();
	}
}

/**
 * ストリーム ヴォリューム設定
 * @param[in] nVolume ヴォリューム(max 100)
 */
void CSoundDeviceWasapi::SetMasterVolume(int nVolume)
{
	if (m_pPCMDevice && m_mastervolume != nVolume)
	{
		m_pPCMDevice->SetMasterVolume(nVolume);
	}
	m_mastervolume = nVolume;
	if (m_pAudioClient)
	{
		// TODO: マスタボリューム設定
	}
}

bool CSoundDeviceWasapi::LoadPCM(UINT nNum, LPCTSTR lpFilename)
{
	return m_pPCMDevice && m_pPCMDevice->LoadPCM(nNum, lpFilename);
}

bool CSoundDeviceWasapi::ReloadPCM(UINT nNum)
{
	return m_pPCMDevice && m_pPCMDevice->ReloadPCM(nNum);
}

void CSoundDeviceWasapi::SetPCMVolume(UINT nNum, int nVolume)
{
	if (m_pPCMDevice) m_pPCMDevice->SetPCMVolume(nNum, nVolume);
}

bool CSoundDeviceWasapi::PlayPCM(UINT nNum, BOOL bLoop)
{
	return m_pPCMDevice && m_pPCMDevice->PlayPCM(nNum, bLoop);
}

void CSoundDeviceWasapi::StopPCM(UINT nNum)
{
	if (m_pPCMDevice) m_pPCMDevice->StopPCM(nNum);
}

void CSoundDeviceWasapi::StopAllPCM()
{
	if (m_pPCMDevice) m_pPCMDevice->StopAllPCM();
}

/**
 * 同期
 * @retval true 継続
 * @retval false 終了
 */
bool CSoundDeviceWasapi::Task()
{
	if(!m_hEvents[0] || !m_hEvents[1]){
		return false;
	}
	switch (::WaitForMultipleObjects(_countof(m_hEvents), m_hEvents, 0, INFINITE))
	{
		case WAIT_OBJECT_0 + 0:
			FillStream();
			break;

		case WAIT_OBJECT_0 + 1:
			return false;

		default:
			break;
	}
	return true;
}

/**
 * ストリームを更新する
 */
void CSoundDeviceWasapi::FillStream()
{
	if (m_pRenderClient)
	{
		UINT32 nFrames = m_nBufferSize;
		if (m_bShared)
		{
			UINT32 nPadding = 0;
			const HRESULT hrPadding = m_pAudioClient->GetCurrentPadding(&nPadding);
			if (FAILED(hrPadding) || (nPadding > m_nBufferSize))
			{
				if (hrPadding == AUDCLNT_E_DEVICE_INVALIDATED) QueueReopen();
				return;
			}
			nFrames -= nPadding;
			if (nFrames == 0)
			{
				return;
			}
		}
		BYTE* pData = NULL;
		HRESULT hr = m_pRenderClient->GetBuffer(nFrames, &pData);
		if (SUCCEEDED(hr))
		{
			if (m_bShared)
			{
				SINT16* pDst = reinterpret_cast<SINT16*>(pData);
				UINT32 nRemaining = nFrames;
				while (nRemaining != 0)
				{
					if (m_nSharedOffset == m_nBufferSize)
					{
						UINT nRead = 0;
						if (m_pSoundData)
						{
							nRead = m_pSoundData->Get16(&m_sharedPCM[0], m_nBufferSize);
						}
						nRead = min(nRead, m_nBufferSize);
						if (nRead < m_nBufferSize)
						{
							ZeroMemory(&m_sharedPCM[nRead * 2], (m_nBufferSize - nRead) * 2 * sizeof(SINT16));
						}
						m_nSharedOffset = 0;
					}
					const UINT32 nCopy = min(nRemaining, m_nBufferSize - m_nSharedOffset);
					CopyMemory(pDst, &m_sharedPCM[m_nSharedOffset * 2], nCopy * 2 * sizeof(SINT16));
					pDst += nCopy * 2;
					m_nSharedOffset += nCopy;
					nRemaining -= nCopy;
				}
			}
			else
			{
				UINT nStreamLength = 0;
				if (m_pSoundData)
				{
					nStreamLength = m_pSoundData->Get16(reinterpret_cast<short*>(pData), nFrames);
				}
				if (nStreamLength < nFrames)
				{
					ZeroMemory(pData + (nStreamLength * m_pwfx->nBlockAlign), (nFrames - nStreamLength) * m_pwfx->nBlockAlign);
				}
			}
			if (m_pRenderClient->ReleaseBuffer(nFrames, 0) == AUDCLNT_E_DEVICE_INVALIDATED)
			{
				QueueReopen();
			}
		}
		else
		{
			if (hr == AUDCLNT_E_DEVICE_INVALIDATED) QueueReopen();
		}
	}
}
