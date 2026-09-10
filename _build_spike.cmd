@ECHO OFF
CD /D C:\Code\LAVFilters
CALL "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=amd64 -no_logo >NUL
CL /nologo /std:c++17 /EHsc C:\Code\LAVFilters\_test_lav_decklink.cpp /Fe:C:\Code\LAVFilters\test_lav_decklink.exe /link strmiids.lib ole32.lib oleaut32.lib
ECHO SPIKE_BUILD_EXIT=%ERRORLEVEL%