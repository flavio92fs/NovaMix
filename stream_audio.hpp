#pragma once
#include <audioclient.h>
#include <avrt.h>
#include <mmreg.h>
#include <cstring>
#include "stream_dsp.hpp"

struct StreamDevice {
    int number;
    std::wstring id;
    std::wstring name;
};

struct StreamSetup {
    std::array<std::wstring, 3> selected;
    std::vector<StreamDevice> devices;
    std::array<int, 2> signal{};

    int number(const std::wstring& id) const {
        for (const auto& device : devices) if (device.id == id) return device.number;
        return 0;
    }

    bool ready() const {
        const auto& pc = selected[0];
        const auto& chat = selected[1];
        const auto& headphones = selected[2];
        return pc != chat && pc != headphones && chat != headphones
            && number(pc) && number(chat) && number(headphones);
    }
};

// Protected by mutexState. The WASAPI thread owns all audio interfaces.
StreamSetup streamSetup;

namespace streams {

void enumerate(IMMDeviceEnumerator& enumerator, std::map<std::wstring, int>& numbers) {
    ComPtr<IMMDeviceCollection> collection;
    check(enumerator.EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection),
          L"Elenco dispositivi di riproduzione");
    UINT count = 0;
    check(collection->GetCount(&count), L"Conteggio dispositivi");
    std::vector<StreamDevice> found;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device))) continue;
        LPWSTR rawId = nullptr;
        if (FAILED(device->GetId(&rawId))) continue;
        std::wstring id(rawId);
        CoTaskMemFree(rawId);
        ComPtr<IPropertyStore> props;
        if (FAILED(device->OpenPropertyStore(STGM_READ, &props))) continue;
        PROPVARIANT property;
        PropVariantInit(&property);
        HRESULT result = props->GetValue(PKEY_Device_FriendlyName, &property);
        std::wstring name = SUCCEEDED(result) && property.vt == VT_LPWSTR
            ? property.pwszVal : L"Dispositivo di riproduzione";
        PropVariantClear(&property);
        int number = numbers.try_emplace(id, int(numbers.size()) + 1).first->second;
        found.push_back({number, std::move(id), std::move(name)});
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return a.name != b.name ? a.name < b.name : a.number < b.number;
    });
    std::lock_guard<std::mutex> lock(mutexState);
    streamSetup.devices = std::move(found);
}

// These GUIDs are the WAVEFORMATEXTENSIBLE PCM and IEEE float subformats.
constexpr GUID pcmSubtype{WAVE_FORMAT_PCM, 0, 0x0010, {0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
constexpr GUID floatSubtype{WAVE_FORMAT_IEEE_FLOAT, 0, 0x0010, {0x80,0,0,0xaa,0,0x38,0x9b,0x71}};

struct InputFormat {
    unsigned channels = 0, bytes = 0;
    bool floating = false;

    static InputFormat read(const WAVEFORMATEX& format) {
        unsigned kind = format.wFormatTag;
        if (kind == WAVE_FORMAT_EXTENSIBLE && format.cbSize >= 22) {
            const auto& extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
            if (IsEqualGUID(extended.SubFormat, pcmSubtype)) kind = WAVE_FORMAT_PCM;
            if (IsEqualGUID(extended.SubFormat, floatSubtype)) kind = WAVE_FORMAT_IEEE_FLOAT;
        }
        bool floating = kind == WAVE_FORMAT_IEEE_FLOAT && format.wBitsPerSample == 32;
        bool pcm = kind == WAVE_FORMAT_PCM &&
            (format.wBitsPerSample == 16 || format.wBitsPerSample == 24 || format.wBitsPerSample == 32);
        unsigned bytes = format.wBitsPerSample / 8;
        if ((!pcm && !floating) || format.nChannels < 1 || format.nChannels > 2 ||
            format.nBlockAlign != bytes * format.nChannels ||
            format.nSamplesPerSec < 8000 || format.nSamplesPerSec > 384000) {
            throw Failure{L"Formato sorgente non supportato. Seleziona mono o stereo nelle proprietà audio di Windows."};
        }
        return {format.nChannels, bytes, floating};
    }

    float sample(const BYTE* data) const {
        if (floating) {
            float value;
            std::memcpy(&value, data, sizeof(value));
            return std::isfinite(value) ? std::clamp(value, -1.f, 1.f) : 0;
        }
        if (bytes == 2) {
            int16_t value;
            std::memcpy(&value, data, sizeof(value));
            return value / 32768.f;
        }
        if (bytes == 3) {
            int value = data[0] | (int(data[1]) << 8) | (int(data[2]) << 16);
            if (value >= 0x800000) value -= 0x1000000;
            return value / 8388608.f;
        }
        int32_t value;
        std::memcpy(&value, data, sizeof(value));
        return float(value / 2147483648.0);
    }

    Frame frame(const BYTE* data) const {
        float left = sample(data);
        return {left, channels == 2 ? sample(data + bytes) : left};
    }
};

ComPtr<IAudioClient> audioClient(IMMDeviceEnumerator& enumerator, const std::wstring& id) {
    ComPtr<IMMDevice> device;
    check(enumerator.GetDevice(id.c_str(), &device), L"Dispositivo audio");
    ComPtr<IAudioClient> client;
    check(device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, nullptr, &client),
          L"Apertura WASAPI");
    return client;
}

class Capture {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    InputFormat input;

public:
    std::unique_ptr<SourceBuffer> buffer;

    Capture(IMMDeviceEnumerator& enumerator, const std::wstring& id) {
        client = audioClient(enumerator, id);
        WAVEFORMATEX* raw = nullptr;
        check(client->GetMixFormat(&raw), L"Formato della sorgente");
        std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> format(raw, CoTaskMemFree);
        input = InputFormat::read(*format);
        REFERENCE_TIME period = 0, minimum = 0;
        check(client->GetDevicePeriod(&period, &minimum), L"Periodo della sorgente");
        unsigned targetMs = unsigned(std::clamp<REFERENCE_TIME>(period / 10000 * 2 + 10, 30, 80));
        buffer = std::make_unique<SourceBuffer>(format->nSamplesPerSec, targetMs);
        check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                 400000, 0, format.get(), nullptr), L"Acquisizione del flusso");
        check(client->GetService(IID_PPV_ARGS(&capture)), L"Buffer sorgente");
    }

    ~Capture() { client->Stop(); }
    void start() { check(client->Start(), L"Avvio sorgente"); }

    void drain() {
        for (int i = 0; i < 32; ++i) {
            UINT32 available = 0;
            check(capture->GetNextPacketSize(&available), L"Lettura sorgente");
            if (!available) return;
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            check(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr),
                  L"Buffer sorgente");
            if (!frames) return;
            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) buffer->discontinuity();
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            for (UINT32 frame = 0; frame < frames; ++frame) {
                buffer->push(silent ? Frame{} : input.frame(data + size_t(frame) * input.bytes * input.channels));
            }
            check(capture->ReleaseBuffer(frames), L"Rilascio buffer sorgente");
        }
    }
};

class Output {
    struct Event {
        HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        ~Event() { if (handle) CloseHandle(handle); }
    } event;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    UINT32 capacity = 0;

public:
    Output(IMMDeviceEnumerator& enumerator, const std::wstring& id) {
        if (!event.handle) throw Failure{L"Impossibile creare l'evento audio."};
        client = audioClient(enumerator, id);
        WAVEFORMATEX format{WAVE_FORMAT_IEEE_FLOAT, 2, outputRate,
                            outputRate * 8, 8, 32, 0};
        constexpr DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST
            | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &format, nullptr),
              L"Formato dell'uscita cuffie");
        check(client->SetEventHandle(event.handle), L"Evento dell'uscita cuffie");
        check(client->GetBufferSize(&capacity), L"Dimensione buffer cuffie");
        check(client->GetService(IID_PPV_ARGS(&render)), L"Buffer cuffie");
    }

    ~Output() { client->Stop(); }
    HANDLE waitHandle() const { return event.handle; }
    UINT32 bufferFrames() const { return capacity; }

    void start() {
        BYTE* data = nullptr;
        check(render->GetBuffer(capacity, &data), L"Buffer iniziale cuffie");
        check(render->ReleaseBuffer(capacity, AUDCLNT_BUFFERFLAGS_SILENT), L"Silenzio iniziale");
        check(client->Start(), L"Avvio cuffie");
    }

    void write(SourceBuffer& pc, SourceBuffer& chat, StereoMix& mixer,
               float pcGain, float chatGain, std::array<float, 2>& peaks) {
        UINT32 pending = 0;
        check(client->GetCurrentPadding(&pending), L"Stato dell'uscita cuffie");
        if (pending >= capacity) return;
        UINT32 frames = capacity - pending;
        BYTE* data = nullptr;
        check(render->GetBuffer(frames, &data), L"Scrittura cuffie");
        auto* samples = reinterpret_cast<float*>(data);
        pc.beginBlock();
        chat.beginBlock();
        for (UINT32 i = 0; i < frames; ++i) {
            auto a = pc.next(), b = chat.next();
            peaks[0] = std::max({peaks[0], std::abs(a.left), std::abs(a.right)});
            peaks[1] = std::max({peaks[1], std::abs(b.left), std::abs(b.right)});
            auto sum = mixer.next(a, b, pcGain, chatGain);
            *samples++ = sum.left;
            *samples++ = sum.right;
        }
        check(render->ReleaseBuffer(frames, 0), L"Invio audio alle cuffie");
    }
};

void play(IMMDeviceEnumerator& enumerator, const std::array<std::wstring, 3>& routes) {
    Capture pc(enumerator, routes[0]);
    Capture chat(enumerator, routes[1]);
    Output output(enumerator, routes[2]);
    pc.start();
    chat.start();
    output.start();
    {
        std::lock_guard<std::mutex> lock(mutexState);
        if (!shared.wantEnabled || WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) return;
        shared.enabled = shared.audio = true;
        shared.audioError.clear();
        shared.notice = L"Flussi PC e Chat attivi.";
    }
    notify();
    struct AudioPriority {
        DWORD index = 0;
        HANDLE task = AvSetMmThreadCharacteristicsW(L"Audio", &index);
        ~AudioPriority() { if (task) AvRevertMmThreadCharacteristics(task); }
    } priority;
    StereoMix mixer;
    std::array<float, 2> peaks{};
    HANDLE waits[]{stopEvent, output.waitHandle(), audioWake};
    ULONGLONG nextReport = 0;
    for (;;) {
        DWORD result = WaitForMultipleObjects(3, waits, FALSE, 1500);
        if (result == WAIT_OBJECT_0) break;
        if (result == WAIT_TIMEOUT) throw Failure{L"L'uscita cuffie non risponde."};
        if (result == WAIT_FAILED) check(HRESULT_FROM_WIN32(GetLastError()), L"Attesa WASAPI");
        mix::Levels levels;
        int master;
        {
            std::lock_guard<std::mutex> lock(mutexState);
            if (!shared.wantEnabled) break;
            levels = shared.levels;
            master = shared.master;
        }
        if (result != WAIT_OBJECT_0 + 1) continue;
        pc.drain();
        chat.drain();
        output.write(*pc.buffer, *chat.buffer, mixer, mix::gain(1, levels.pc, master),
                     mix::gain(1, levels.chat, master), peaks);
        auto now = GetTickCount64();
        if (now >= nextReport) {
            std::wostringstream report;
            report << L"WASAPI stereo 48 kHz, buffer uscita " << output.bufferFrames()
                   << L" campioni.\nBuffer riavviati PC/Chat: "
                   << pc.buffer->underruns << L"/" << chat.buffer->underruns
                   << L"\nCampioni scartati PC/Chat: "
                   << pc.buffer->dropped << L"/" << chat.buffer->dropped << L"\n";
            {
                std::lock_guard<std::mutex> lock(mutexState);
                for (size_t i = 0; i < 2; ++i) streamSetup.signal[i] = int(std::lround(peaks[i] * 100));
                shared.audioDetails = report.str();
            }
            peaks = {};
            nextReport = now + 250;
        }
    }
}
}

void audioWorker() {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) {
        std::lock_guard<std::mutex> lock(mutexState);
        shared.audioError = L"Inizializzazione audio: " + errorText(initialized);
        return;
    }
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        std::map<std::wstring, int> numbers;
        HANDLE waits[]{stopEvent, audioWake};
        ULONGLONG nextRefresh = 0;
        while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0) {
            try {
                if (!enumerator) {
                    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&enumerator)), L"Dispositivi audio Windows");
                }
                if (GetTickCount64() >= nextRefresh) {
                    streams::enumerate(*enumerator.Get(), numbers);
                    nextRefresh = GetTickCount64() + 2000;
                }
                std::array<std::wstring, 3> routes;
                bool requested;
                {
                    std::lock_guard<std::mutex> lock(mutexState);
                    requested = shared.wantEnabled;
                    if (requested && !streamSetup.ready()) throw Failure{L"Servono tre uscite disponibili e distinte."};
                    if (requested) routes = streamSetup.selected;
                }
                if (requested) {
                    streams::play(*enumerator.Get(), routes);
                    std::lock_guard<std::mutex> lock(mutexState);
                    shared.enabled = shared.audio = shared.wantEnabled = false;
                    streamSetup.signal = {};
                    shared.notice = L"Flussi fermi.";
                    nextRefresh = 0;
                }
            } catch (const Failure& error) {
                std::lock_guard<std::mutex> lock(mutexState);
                shared.enabled = shared.audio = shared.wantEnabled = false;
                streamSetup.signal = {};
                shared.audioError = error.message;
                nextRefresh = 0;
            }
            notify();
            if (WaitForMultipleObjects(2, waits, FALSE, 2000) == WAIT_OBJECT_0) break;
        }
    }
    CoUninitialize();
}
