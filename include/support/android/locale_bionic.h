// -*- C++ -*-
//===------------------- support/android/locale_bionic.h ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_SUPPORT_ANDROID_LOCALE_BIONIC_H
#define _LIBCPP_SUPPORT_ANDROID_LOCALE_BIONIC_H

#if defined(__ANDROID__)

#ifdef __cplusplus
extern "C" {
#endif

#include <xlocale.h>

#define isalnum_l(c, locale)                   isalnum(c)
#define isalpha_l(c, locale)                   isalpha(c)
#define isascii_l(c, locale)                   isascii(c)
#define isblank_l(c, locale)                   isblank(c)
#define iscntrl_l(c, locale)                   iscntrl(c)
#define isdigit_l(c, locale)                   isdigit(c)
#define isgraph_l(c, locale)                   isgraph(c)
#define islower_l(c, locale)                   islower(c)
#define isprint_l(c, locale)                   isprint(c)
#define ispunct_l(c, locale)                   ispunct(c)
#define isspace_l(c, locale)                   isspace(c)
#define isupper_l(c, locale)                   isupper(c)
#define isxdigit_l(c, locale)                  isxdigit(c)
#define iswalnum_l(c, locale)                  iswalnum(c)
#define iswalpha_l(c, locale)                  iswalpha(c)
#define iswascii_l(c, locale)                  iswascii(c)
#define iswblank_l(c, locale)                  iswblank(c)
#define iswcntrl_l(c, locale)                  iswcntrl(c)
#define iswdigit_l(c, locale)                  iswdigit(c)
#define iswgraph_l(c, locale)                  iswgraph(c)
#define iswlower_l(c, locale)                  iswlower(c)
#define iswprint_l(c, locale)                  iswprint(c)
#define iswpunct_l(c, locale)                  iswpunct(c)
#define iswspace_l(c, locale)                  iswspace(c)
#define iswupper_l(c, locale)                  iswupper(c)
#define iswxdigit_l(c, locale)                 iswxdigit(c)
#define toupper_l(c, locale)                   toupper(c)
#define tolower_l(c, locale)                   tolower(c)
#define towupper_l(c, locale)                  towupper(c)
#define towlower_l(c, locale)                  towlower(c)
#define strcoll_l(s1, s2, locale)              strcoll(s1, s2)
#define strxfrm_l(dest, src, n, locale)        strxfrm(dest, src, n)
#define strftime_l(s, max, format, tm, locale) strftime(s, max, format, tm)
#define wcscoll_l(s1, s2, locale)              wcscoll(s1, s2)
#define wcsxfrm_l(dest, src, n, locale)        wcsxfrm(dest, src, n)
#define strtold_l(nptr, endptr, locale)        strtold(nptr, endptr)
#define strtoll_l(nptr, endptr, base, locale)  strtoll(nptr, endptr, base)
#define strtoull_l(nptr, endptr, base, locale) strtoull(nptr, endptr, base)
#define wcstoll_l(nptr, endptr, locale)        wcstoll(nptr, endptr)
#define wcstoull_l(nptr, endptr, locale)       wcstoull(nptr, endptr)
#define wcstold_l(nptr, endptr, locale)        wcstold(nptr, endptr)

#ifdef __cplusplus
}
#endif
#endif // defined(__ANDROID__)
#endif // _LIBCPP_SUPPORT_ANDROID_LOCALE_BIONIC_H
