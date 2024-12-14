#ifndef __TVP_WINDOWS_EXCEPTION_H
#define __TVP_WINDOWS_EXCEPTION_H


#include <windows.h>
#include "tp_stub.h"


inline void TVPThrowExceptionMessage(HRESULT hr) {
	static wchar_t buffer[1024] = {0};

    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;

    DWORD err = HRESULT_CODE(hr);

    DWORD len = FormatMessageW(
        flags,
        nullptr,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buffer,
        sizeof(buffer) / sizeof(wchar_t) - 1,
        nullptr
    );

	TVPThrowExceptionMessage(buffer);
}

#endif
