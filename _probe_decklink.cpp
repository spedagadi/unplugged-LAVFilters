// _probe_decklink.cpp — direct probe of the ffmpeg decklink indev exactly the
// way LAVSplitter invokes it: bare device name + AVDictionary query params.
// Bypasses LAV so every av_log line surfaces with the failing SDK call.
#define __STDC_CONSTANT_MACROS
#define __STDC_FORMAT_MACROS
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

typedef void  (*f_av_log_set_callback)(void (*cb)(void*, int, const char*, va_list));
typedef void  (*f_av_log_set_level)(int);
typedef void* (*f_av_find_input_format)(const char*);
typedef int   (*f_avformat_open_input)(void**, const char*, void*, void**);
typedef void  (*f_avdevice_register_all)(void);
typedef int   (*f_av_dict_set)(void**, const char*, const char*, int);
typedef int   (*f_av_strerror)(int, char*, size_t);
typedef int   (*f_avformat_find_stream_info)(void*, void**);
typedef int   (*f_av_read_frame)(void*, void*);

static int log_level = 56; // AV_LOG_DEBUG

static void log_cb(void* ptr, int level, const char* fmt, va_list args)
{
    if (level <= log_level) {
        char buf[4096];
        vsnprintf(buf, sizeof(buf), fmt, args);
        fprintf(stderr, "[ffmpeg] %s\n", buf);
    }
}

typedef HRESULT (WINAPI *f_CoInitializeEx)(LPVOID, DWORD);
typedef void (WINAPI *f_CoUninitialize)(void);

int main(int argc, char* argv[])
{
    // Replicate the spike's COM apartment exactly:
    //   CoInitializeEx(nullptr, COINIT_MULTITHREADED) before everything.
    HRESULT hrCom = S_FALSE;
    if (argc > 3 && argv[3][0] == '1') {
        f_CoInitializeEx ci = (f_CoInitializeEx)GetProcAddress(GetModuleHandleA("ole32.dll"), "CoInitializeEx");
        f_CoUninitialize  cu = (f_CoUninitialize)GetProcAddress(GetModuleHandleA("ole32.dll"), "CoUninitialize");
        hrCom = ci ? ci(NULL, 0 /*COINIT_MULTITHREADED*/) : (HRESULT)E_FAIL;
        printf("[probe] CoInitializeEx(MTA) hr=0x%08X\n", (unsigned)hrCom);
    } else {
        printf("[probe] COM left uninitialized (decklink's CoInitialize->STA)\n");
    }

    const char* device = (argc > 1) ? argv[1] : "DeckLink Quad HDMI Recorder (1)";
    const char* axdir  = "C:\\code\\LAVFilters\\bin_x64";
    SetDllDirectoryA(axdir);

    HMODULE hAVF = LoadLibraryA("avformat-lav-63.dll");
    HMODULE hAVD = LoadLibraryA("avdevice-lav-63.dll");
    HMODULE hAVU = LoadLibraryA("avutil-lav-61.dll");
    printf("DLLs: avformat=%p avdevice=%p avutil=%p err=%lu\n", (void*)hAVF, (void*)hAVD, (void*)hAVU, (unsigned long)GetLastError());
    if (!hAVF || !hAVD || !hAVU) return 1;

    auto set_cb    = (f_av_log_set_callback)GetProcAddress(hAVU, "av_log_set_callback");
    auto set_level = (f_av_log_set_level)GetProcAddress(hAVU, "av_log_set_level");
    auto find_fmt  = (f_av_find_input_format)GetProcAddress(hAVF, "av_find_input_format");
    auto open_in   = (f_avformat_open_input)GetProcAddress(hAVF, "avformat_open_input");
    auto reg_all   = (f_avdevice_register_all)GetProcAddress(hAVD, "avdevice_register_all");
    auto dict_set  = (f_av_dict_set)GetProcAddress(hAVU, "av_dict_set");
    auto strerr    = (f_av_strerror)GetProcAddress(hAVU, "av_strerror");
    printf("exports: cb=%p lv=%p ff=%p oi=%p ra=%p ds=%p se=%p\n", (void*)set_cb, (void*)set_level, (void*)find_fmt, (void*)open_in, (void*)reg_all, (void*)dict_set, (void*)strerr);
    if (!set_cb || !set_level || !find_fmt || !open_in || !reg_all || !dict_set || !strerr) return 1;

    set_cb(log_cb);
    reg_all();
    void* fmt = find_fmt("decklink");
    printf("[probe] av_find_input_format(decklink)=%p\n", fmt);
    if (!fmt) { printf("[probe] indev NOT registered!\n"); return 1; }

    // Exactly LAVSplitter's generic options dict (merge order matters):
    void* dict = NULL;
    dict_set(&dict, "icy", "1", 0);
    dict_set(&dict, "advanced_editlist", "0", 0);
    dict_set(&dict, "reconnect", "1", 0);
    dict_set(&dict, "skip_clear", "1", 0);
    dict_set(&dict, "max_reload", "7", 0);
    dict_set(&dict, "extension_picky", "0", 0);
    dict_set(&dict, "referer", device, 0);  // LAV: self-referer = device name
    // then decklink:// query params override:
    dict_set(&dict, "video_input", "hdmi", 0);
    dict_set(&dict, "signal_loss_action", "repeat", 0);
    dict_set(&dict, "no_audio", "1", 0);
    dict_set(&dict, "raw_format", "uyvy422", 0);
    dict_set(&dict, "loglevel", "debug", 0);
    set_level(log_level);

    printf("[probe] opening device '%s'\n", device);

    printf("[probe] opening device '%s'\n", device);
    void* ctx = NULL;
    int ret = open_in(&ctx, device, fmt, &dict);
    printf("[probe] avformat_open_input ret=%d\n", ret);
    char errbuf[128] = {};
    strerr(ret, errbuf, sizeof(errbuf));
    printf("[probe] av_strerror='%s'\n", errbuf);
    printf("[probe] ctx=%p\n", (void*)ctx);

    // Replicate LAV's InitAVFormat stream-info step to catch the same crash:
    auto find_si = (f_avformat_find_stream_info)GetProcAddress(hAVF, "avformat_find_stream_info");
    if (find_si && ctx) {
        printf("[probe] calling avformat_find_stream_info (reads live frames)...\n");
        fflush(stdout);
        int r2 = find_si(ctx, NULL);
        printf("[probe] avformat_find_stream_info ret=%d nb_streams now available\n", r2);
        fflush(stdout);
    }
    if (argc > 3 && argv[3][0] == '1') {
        f_CoUninitialize cu = (f_CoUninitialize)GetProcAddress(GetModuleHandleA("ole32.dll"), "CoUninitialize");
        if (cu) cu();
    }
    return ret ? 1 : 0;
}