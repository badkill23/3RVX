#include "CoreAudio.h"
#include "Functiondiscoverykeys_devpkey.h"
#include "../../Logger.h"

HRESULT CoreAudio::Init() {
    HRESULT hr;

    hr = m_devEnumerator.CoCreateInstance(__uuidof(MMDeviceEnumerator));
    if (SUCCEEDED(hr)) {
        hr = m_devEnumerator->RegisterEndpointNotificationCallback(this);

        if (SUCCEEDED(hr)) {
            hr = AttachDefaultDevice();
        }
    }

    return hr;
}

void CoreAudio::Dispose() {
    DetachCurrentDevice();
    m_devEnumerator->UnregisterEndpointNotificationCallback(this);
}

HRESULT CoreAudio::AttachDefaultDevice() {
    m_critSect.Enter();

    HRESULT hr;
    hr = m_devEnumerator->GetDefaultAudioEndpoint(eRender,
        eMultimedia, &m_device);

    if (SUCCEEDED(hr)) {
        hr = m_device->Activate(__uuidof(m_volumeControl),
            CLSCTX_INPROC_SERVER, NULL, (void**)&m_volumeControl);

        CLOG(L"Attached to audio device: [%s]", DeviceName().c_str());

        if (SUCCEEDED(hr)) {
            hr = m_volumeControl->RegisterControlChangeNotify(this);
            m_registeredNotifications = SUCCEEDED(hr);
        }
    } else {
        CLOG(L"Failed to find default audio device!");
    }

    m_critSect.Leave();
    return hr;
}

void CoreAudio::DetachCurrentDevice() {
    m_critSect.Enter();

    if (m_volumeControl != NULL) {

        if (m_registeredNotifications) {
            m_volumeControl->UnregisterControlChangeNotify(this);
            m_registeredNotifications = false;
        }

        m_volumeControl.Release();
    }

    if (m_device != NULL) {
        m_device.Release();
    }

    m_critSect.Leave();
}

HRESULT CoreAudio::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
    PostMessage(m_notifyHwnd, MSG_VOL_CHNG, 0, 0);
    return S_OK;
}

HRESULT CoreAudio::OnDefaultDeviceChanged(
    EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) {
    if (flow == eRender) {
        PostMessage(m_notifyHwnd, MSG_VOL_DEVCHNG, 0, 0);
    }

    return S_OK;
}

void CoreAudio::ReattachDefaultDevice() {
    DetachCurrentDevice();
    AttachDefaultDevice();
}

std::wstring CoreAudio::DeviceName() {
    HRESULT hr;
    LPWSTR devId;

    m_device->GetId(&devId);

    IPropertyStore *props = NULL;
    hr = m_device->OpenPropertyStore(STGM_READ, &props);
    PROPVARIANT pvName;
    PropVariantInit(&pvName);
    props->GetValue(PKEY_Device_FriendlyName, &pvName);

    std::wstring str(pvName.pwszVal);
    CoTaskMemFree(devId);
    PropVariantClear(&pvName);
    props->Release();

    return str;
}

float CoreAudio::Volume() {
    float vol = 0.0f;
    _volumeControl->GetMasterVolumeLevelScalar(&vol);
    return vol;
}

void CoreAudio::Volume(float vol) {
    if (vol > 1.0f) {
        vol = 1.0f;
    }

    if (vol < 0.0f) {
        vol = 0.0f;
    }

    _volumeControl->SetMasterVolumeLevelScalar(vol, NULL);
}

bool CoreAudio::Muted() {
    BOOL muted = FALSE;
    _volumeControl->GetMute(&muted);

    return (muted == TRUE) ? true : false;
}

void CoreAudio::Muted(bool muted) {
    _volumeControl->SetMute(muted, NULL);
}

ULONG CoreAudio::AddRef() {
    return InterlockedIncrement(&_refCount);
}

ULONG CoreAudio::Release() {
    long lRef = InterlockedDecrement(&_refCount);
    if (lRef == 0) {
        delete this;
    }
    return lRef;
}

HRESULT CoreAudio::QueryInterface(REFIID iid, void **ppUnk) {
    if ((iid == __uuidof(IUnknown)) ||
        (iid == __uuidof(IMMNotificationClient))) {
        *ppUnk = static_cast<IMMNotificationClient*>(this);
    } else if (iid == __uuidof(IAudioEndpointVolumeCallback)) {
        *ppUnk = static_cast<IAudioEndpointVolumeCallback*>(this);
    } else {
        *ppUnk = NULL;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}