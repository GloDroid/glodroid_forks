// -*- C++ -*-
//===------------------- support/android/wchar_bionic.h -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_SUPPORT_ANDROID_WCHAR_BIONIC_H
#define _LIBCPP_SUPPORT_ANDROID_WCHAR_BIONIC_H

#ifdef __ANDROID__

#ifdef __cplusplus
extern "C" {
#endif

int vfwscanf(FILE *, const wchar_t *, va_list);
int vswscanf(const wchar_t *, const wchar_t *, va_list);
int vwscanf(const wchar_t *, va_list);
float wcstof(const wchar_t *, wchar_t **);
double wcstod(const wchar_t *, wchar_t **);
long double wcstold(const wchar_t *, wchar_t **);
long long wcstoll(const wchar_t *, wchar_t **, int);
unsigned long long wcstoull(const wchar_t *, wchar_t **, int);
size_t wcsnrtombs(char *, const wchar_t **, size_t, size_t, mbstate_t *);
size_t mbsnrtowcs(wchar_t *, const char **, size_t, size_t, mbstate_t *);
int mbtowc(wchar_t *, const char *, size_t);

#ifdef __cplusplus
}
#endif
#endif // __ANDROID__
#endif // _LIBCPP_SUPPORT_ANDROID_WCHAR_BIONIC_H
