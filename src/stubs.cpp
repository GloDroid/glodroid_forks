// -*- C++ -*-
//===----------------------------- stubs.cpp ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifdef __ANDROID__

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include <wchar.h>

#include <support/android/nl_types.h>

static void unimplemented_stub(const char* function) {
    const char* fmt = "%s(3) is not implemented on Android\n";
    fprintf(stderr, fmt, function);
}

#define UNIMPLEMENTED unimplemented_stub(__PRETTY_FUNCTION__)

float wcstof(const wchar_t *, wchar_t **)
{
    UNIMPLEMENTED;
    return 0;
}

double wcstod(const wchar_t *, wchar_t **)
{
    UNIMPLEMENTED;
    return 0;
}

long double wcstold(const wchar_t *, wchar_t **)
{
    UNIMPLEMENTED;
    return 0;
}

long long wcstoll(const wchar_t *, wchar_t **, int)
{
    UNIMPLEMENTED;
    return 0;
}

unsigned long long wcstoull(const wchar_t *, wchar_t **, int)
{
    UNIMPLEMENTED;
    return 0;
}

size_t wcsnrtombs(char *, const wchar_t **, size_t, size_t, mbstate_t *)
{
    UNIMPLEMENTED;
    errno = EILSEQ;
    return (size_t)-1;
}

size_t mbsnrtowcs(wchar_t *, const char **, size_t, size_t, mbstate_t *)
{
    UNIMPLEMENTED;
    errno = EILSEQ;
    return (size_t)-1;
}

int mbtowc(wchar_t *, const char *, size_t)
{
    UNIMPLEMENTED;
    return -1;
}

nl_catd catopen(const char *, int)
{
    UNIMPLEMENTED;
    return (nl_catd)-1;
}

int catclose(nl_catd)
{
    UNIMPLEMENTED;
    return -1;
}

char *catgets(nl_catd, int, int, const char *message)
{
    UNIMPLEMENTED;
    return (char *)message;
}

#ifdef __cplusplus
}
#endif

#endif // __ANDROID__
