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

#ifdef __cplusplus
}
#endif
#endif // __ANDROID__
#endif // _LIBCPP_SUPPORT_ANDROID_WCHAR_BIONIC_H
