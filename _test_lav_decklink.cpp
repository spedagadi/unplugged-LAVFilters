// test_lav_decklink.cpp — standalone spike: does LAVSplitterSource open a
// decklink:// URL on THIS machine? Isolates LAV/avdevice from the s3_shell
// stack. Reports a distinct HRESULT per failure stage so the log is
// self-diagnosing:
//   0x80040154 REGDB_E_CLASSNOTREG — av_find_input_format("decklink") null
//     (avdevice_register_all() not taking effect / wrong LAVSplitter.ax)
//   0x8004_01xx — avformat_open_input failed; low byte = errno (6=ENXIO
//     device not found, 5=EIO, 4=EINTR, 62=ETIMEDOUT, ...)
//   S_OK — LAVSplitterSource demuxed the URL; pins enumerated below.
//
// Build (from a VS x64 dev prompt, LAV bin in PATH):
//   cl /nologo /std:c++17 /EHsc test_lav_decklink.cpp /link \
//       strmiids.lib ole32.lib oleaut32.lib /out:test_lav_decklink.exe
//
// Usage:
//   test_lav_decklink.exe "DeckLink Quad HDMI Recorder (1)"

#include <windows.h>
#include <winver.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <atlbase.h>
#include <stdio.h>

#include <string>

static const CLSID CLSID_LAVSplitterSource = {0xB98D13E7, 0x55DB, 0x4385, {0xA3, 0x3D, 0x09, 0xFD, 0x1B, 0xA2, 0x63, 0x38}};

static const char* HrName(HRESULT hr)
{
    switch ((unsigned)hr) {
    case 0x00000000: return "S_OK";
    case 0x80040154: return "REGDB_E_CLASSNOTREG (indev not registered / avdevice_register_all no-op)";
    default: {
        if ((hr & 0xFFFF0000) == MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0))
            return "FACILITY_ITF low-word = AVERROR errno (6=ENXIO 5=EIO 4=EINTR 62=ETIMEDOUT 110=unknown)";
        return "(see hex)";
    }
    }
}

static void PrintMediaType(const AM_MEDIA_TYPE* mt)
{
    if (!mt) return;
    char guid[64];
    sprintf_s(guid, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        mt->subtype.Data1, mt->subtype.Data2, mt->subtype.Data3,
        mt->subtype.Data4[0], mt->subtype.Data4[1], mt->subtype.Data4[2], mt->subtype.Data4[3],
        mt->subtype.Data4[4], mt->subtype.Data4[5], mt->subtype.Data4[6], mt->subtype.Data4[7]);
    printf("    subtype %s\n", guid);
    if (mt->formattype == FORMAT_VideoInfo2 && mt->pbFormat) {
        const VIDEOINFOHEADER2* v = (const VIDEOINFOHEADER2*)mt->pbFormat;
        printf("    %dx%d itl=%d\n", v->bmiHeader.biWidth, abs(v->bmiHeader.biHeight),
               (v->dwInterlaceFlags & AMINTERLACE_IsInterlaced) ? 1 : 0);
    } else if (mt->formattype == FORMAT_VideoInfo && mt->pbFormat) {
        const VIDEOINFOHEADER* v = (const VIDEOINFOHEADER*)mt->pbFormat;
        printf("    %dx%d\n", v->bmiHeader.biWidth, abs(v->bmiHeader.biHeight));
    }
}

static void EnumeratePins(IBaseFilter* f)
{
    CComPtr<IEnumPins> pins;
    HRESULT hr = f->EnumPins(&pins);
    printf("  EnumPins hr=0x%08X\n", (unsigned)hr);
    if (FAILED(hr) || !pins) return;
    CComPtr<IPin> pin;
    ULONG got = 0;
    int idx = 0;
    while (pins->Next(1, &pin, &got) == S_OK) {
        PIN_DIRECTION d;
        if (SUCCEEDED(pin->QueryDirection(&d))) {
            printf("  pin[%d] %s\n", idx, d == PINDIR_OUTPUT ? "OUTPUT" : "INPUT");
            if (d == PINDIR_OUTPUT) {
                AM_MEDIA_TYPE mt = {};
                if (SUCCEEDED(pin->ConnectionMediaType(&mt))) {
                    PrintMediaType(&mt);
                    if (mt.cbFormat && mt.pbFormat) CoTaskMemFree(mt.pbFormat);
                }
            }
        }
        pin.Release();
        idx++;
    }
}

int wmain(int argc, wchar_t* argv[])
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // match LAV's usage
    printf("[spike] CoInitializeEx hr=0x%08X\n", (unsigned)hr);

    if (argc < 2) {
        printf("[spike] usage: test_lav_decklink.exe \"DeckLink device name\"\n");
        return 2;
    }
    std::wstring device = argv[1];
    std::wstring url = L"decklink://" + device +
        L"?video_input=hdmi&signal_loss_action=repeat&no_audio=1&raw_format=uyvy422";
    printf("[spike] url='%S'\n", url.c_str());

    // Load the splitter DLL by EXPLICIT path + DllGetClassObject so we know
    // exactly which file is running (no COM-cache / stale-registration doubt).
    const wchar_t* axPath = L"C:\\Code\\LAVFilters\\bin_x64\\LAVSplitter.ax";
    if (argc >= 3) axPath = argv[2];
    {
        wchar_t axdir[MAX_PATH] = {};
        wcscpy_s(axdir, axPath);
        wchar_t* sl = wcsrchr(axdir, L'\\');
        if (sl) *sl = 0;
        SetDllDirectoryW(axdir);   // sidecars (avformat-lav-63.dll, avdevice…) next to the .ax
    }
    HMODULE h = LoadLibraryW(axPath);
    printf("[spike] LoadLibrary('%S') h=%p err=%lu\n", axPath, (void*)h, (unsigned long)GetLastError());
    if (!h) return 1;

    // Print the file version of the loaded .ax so we know exactly which build ran.
    {
        wchar_t modPath[MAX_PATH] = {};
        GetModuleFileNameW(h, modPath, MAX_PATH);
        DWORD verSize = GetFileVersionInfoSizeW(modPath, nullptr);
        if (verSize) {
            void* ver = malloc(verSize);
            if (GetFileVersionInfoW(modPath, 0, verSize, ver)) {
                VS_FIXEDFILEINFO* ffi = nullptr; UINT ffiLen = 0;
                if (VerQueryValueW(ver, L"\\", (void**)&ffi, &ffiLen) && ffi) {
                    printf("[spike] loaded version: %u.%u.%u.%u\n",
                        HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
                        HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
                }
            }
            free(ver);
        }
        printf("[spike] loaded module: %S\n", modPath);
    }
    auto pDllGetClassObject = (HRESULT(WINAPI*)(const CLSID&, const IID&, void**))GetProcAddress(h, "DllGetClassObject");
    if (!pDllGetClassObject) { printf("[spike] DllGetClassObject not exported\n"); return 1; }

    CComPtr<IClassFactory> cf;
    hr = pDllGetClassObject(CLSID_LAVSplitterSource, IID_IClassFactory, (void**)&cf);
    printf("[spike] DllGetClassObject(LAVSplitterSource) hr=0x%08X\n", (unsigned)hr);
    if (FAILED(hr)) return 1;

    CComPtr<IBaseFilter> src;
    hr = cf->CreateInstance(nullptr, IID_PPV_ARGS(&src));
    printf("[spike] CreateInstance hr=0x%08X  %s\n", (unsigned)hr, HrName(hr));
    if (FAILED(hr)) { printf("[spike] LAVSplitterSource instance failed\n"); return 1; }

    CComQIPtr<IFileSourceFilter> pFS = src;
    if (!pFS) { printf("[spike] QI IFileSourceFilter failed (wrong CLSID?)\n"); return 1; }

    printf("[spike] IFileSourceFilter::Load(decklink URL)...\n");
    hr = pFS->Load(url.c_str(), nullptr);
    printf("[spike] Load hr=0x%08X  %s\n", (unsigned)hr, HrName(hr));
    if (FAILED(hr)) {
        printf("[spike] ---- LAVSplitterSource could NOT open decklink:// ----\n");
        return 1;
    }

    printf("[spike] Load OK — enumerating output pins:\n");
    EnumeratePins(src);
    printf("[spike] ---- LAVSplitterSource OPENED and demuxed the URL ----\n");
    return 0;
}