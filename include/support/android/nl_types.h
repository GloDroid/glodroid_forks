// -*- C++ -*-
//===--------------------- support/android/nl_types.h ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef _LIBCPP_SUPPORT_ANDROID_NL_TYPES_H
#define _LIBCPP_SUPPORT_ANDROID_NL_TYPES_H

#ifdef __ANDROID__

#ifdef __cplusplus
extern "C" {
#endif

#define NL_SETD       1
#define NL_CAT_LOCALE 1

typedef void *nl_catd;
typedef int nl_item;

nl_catd catopen(const char *, int);
int catclose(nl_catd);
char *catgets(nl_catd, int, int, const char *);

#ifdef __cplusplus
}
#endif

#endif // __ANDROID__
#endif // _LIBCPP_SUPPORT_ANDROID_NL_TYPES_H
