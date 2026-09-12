---
name: build-lavfilters
description: Build LAVFilters x64 (LAVSplitter.ax / LAVVideo.ax / LAVAudio.ax) on the unplugged fork. Use when asked to build/rebuild the LAV DirectShow filters, rebuild after LAVSplitterSource or ffmpeg decklink changes, or fix filter compile/link errors.
---

# Build LAVFilters (DeckLink fork) — x64 filters

Target artifact: `bin_x64\LAVSplitter.ax`, `bin_x64\LAVVideo.ax`, `bin_x64\LAVAudio.ax`

This fork's LAVSplitterSource understands `decklink://<device>?…` URLs and links
the FFmpeg `libavdevice/decklink_dec` capture source (branch `master`, ffmpeg
submodule on `lav0.83-decklink`).

## Two build paths — pick the right one

The root `build.bat` is the **full LAV build**: it runs `build_ffmpeg.sh` (a clean
ffmpeg rebuild through MSYS2 for Win32 AND x64 — 30+ min, needs the mingw gcc
toolchain and pkg-config) and then rebuilds the filters. You only need the ffmpeg
rebuild when the **ffmpeg decklink source changed**. Otherwise use the fast path:

### Fast path — filters against the prebuilt ffmpeg libs (usual)

```powershell
Push-Location 'C:\code\LAVFilters'
MSBuild.exe LAVFilters.sln /p:Configuration=Release /p:Platform=x64 /m /nologo /verbosity:minimal
Pop-Location
```
- Depends on ffmpeg libs already present in `bin_x64\` (avcodec-lav-63.dll,
  avdevice-lav-63.dll, … + `bin_x64\lib\*.lib`). These live in the repo copy and
  contain the decklink no_audio patch.
- The x64 filter DLLs link against those; our pipeline (s3_shell) is x64-only, so
  the Win32 leg is not needed.

### Full path — rebuild ffmpeg from source (when decklink_dec changed)

The original ffmpeg build was **done with the UCRT64 toolchain** (the shipped
`bin_x64\*-lav-*.dll` import `api-ms-win-crt-*`). It is replicable here, one
alias step + one invocation:

```powershell
# 1. ucrt64 ships binutils UNPREFIXED (in ucrt64\x86_64-w64-mingw32\bin\) but
#    build_ffmpeg.sh hard-codes prefixed names (x86_64-w64-mingw32-windres/ar/...).
#    Create one-time prefixed aliases (already done on this box → skip if present):
$toolLinks = 'C:\code\LAVFilters\build_tools\bin'
New-Item -ItemType Directory -Force $toolLinks | Out-Null
$src = 'C:\msys64\ucrt64\x86_64-w64-mingw32\bin'
foreach ($n in @('ar','ranlib','strip','ld','dlltool','windres','nm','objcopy','objdump','as','readelf')) {
  & 'C:\msys64\usr\bin\ln.exe' -sf "$src\$n.exe" "$toolLinks\x86_64-w64-mingw32-$n.exe"
}

# 2. Build x64 ONLY (x86 leg needs the i686 toolchain we don't have). PATH must be
#    set INSIDE the bash call — a login shell resets it, so plain `cmd /c build.bat`
#    or a PowerShell-inherited env WILL fail with "x86_64-w64-mingw32-gcc command
#    not found" in ffbuild/config.log.
& 'C:\msys64\usr\bin\bash.exe' -lc 'export PATH=/c/code/LAVFilters/build_tools/bin:/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH; cd /c/code/LAVFilters && which x86_64-w64-mingw32-gcc && sh build_ffmpeg.sh x64 2>&1 | tee _ffmpeg_rebuild.log'
```
- `build_ffmpeg.sh x64` does the full clean configure + `make -j$NUMBER_OF_PROCESSORS`
  and copies results into `bin_x64\` / `bin_x64d\` (overwrites the DLLs!). Configure
  output lands in `ffmpeg\ffbuild\config.out` (the script redirects); the main
  banner only shows on the tee'd log.
- ~15-40 min. Backup rollback point: `_bin_x64_prerebuild\` (pre-build DLL copy).
- The `make` step needs `WINDRES="x86_64-w64-mingw32-windres --use-temp-file"`
  (works around an MSYS2 binutils pipe bug) — the script already sets it.

Not needed for routine filter work — only after editing `libavdevice/decklink_dec.cpp`.

## Critical gotchas on a copied/cloned tree

- **Stale PCH/obj from the source machine.** A copied `bin_x64\` carries `.pch`,
  `.obj`, `.ipch` built by an older MSVC; the first build dies with
  `error C1853: '...bin_x64\Demuxers\demuxers.pch' precompiled header ... different
  version of the compiler`. Fix before building:
  ```powershell
  Get-ChildItem 'C:\code\LAVFilters\bin_x64' -Include '*.pch','*.obj','*.ipch' -Recurse -Force |
      Remove-Item -Force
  ```
- **The ffmpeg submodule commit is only in local copies.** `270f7d55e9`
  (`lav0.83-decklink`) is NOT pushed upstream (origin is LAV's private
  gitea + local changes). A fresh `git clone` cannot fetch it — copy the whole
  `LAVFilters` folder including the submodule checkout, don't clone. Once the
  folder is present, `git submodule status` should show ffmpeg at `270f7d55e9`.
- **Win32 build needs the i686 mingw toolchain** which this box lacks (ucrt64 is
  x86_64-only). Fine for our x64 pipeline; don't attempt the Win32 leg.

## Verify

```powershell
Get-Item 'C:\code\LAVFilters\bin_x64\LAVSplitter.ax','C:\code\LAVFilters\bin_x64\LAVVideo.ax','C:\code\LAVFilters\bin_x64\LAVAudio.ax'
# Fresh LastWriteTime. LAVSplitter.ax ~687 KB, LAVVideo.ax ~1.28 MB, LAVAudio.ax ~337 KB
```

## Runtime wiring

The engine/s3_shell find LAVSplitter by CLSID at run time — the fork's
`LAVSplitter.ax` must be registered (elevated):
```
regsvr32 /s C:\code\LAVFilters\bin_x64\LAVSplitter.ax
```
Do NOT register the bundled upstream LAV from mpc-hc's `src\thirdparty\LAVFilters`
(it has no decklink support).