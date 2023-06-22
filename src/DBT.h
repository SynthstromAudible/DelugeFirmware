/* The actual DBT.h will be specified higher in / outside the search path, overriding 
this template. Was going to use #pragma once, but this is a legitimate use case for the
classic #ifndef guard. */

#ifndef DBT_H
#define DBT_H

/* This template is used by DBT to substitute preprocessor variables into specific builds. 

While DBT itself can manage this through the command line, this is mainly for external
build environments such as e2 Studio which DBT has limited ability to influence.

Run `dbt.cmd --dry_run --silent` prior to the build in e2 to have DBT manage creation of
the requried override file for e2.

This template file is not strictly necessary but is checked in mainly to assist in hinting
for Intellisense and other smart editing tools.
*/

/* Defines to allow a build to succeed even if the substitution is missed. */

#define DBT_TMPL_VERSION_STRING "UnknownVersion"
#define DBT_TMPL_BUILD_RELEASE 0
#define DBT_TMPL_HAVE_OLED 0

/* Defines using placeholders which will be substituted: */

#define DBT_FW_VERSION_STRING (DBT_TMPL_VERSION_STRING)
#define DBT_BUILD_RELEASE (DBT_TMPL_BUILD_RELEASE)
#define HAVE_OLED (DBT_TMPL_HAVE_OLED)

/* Logic to cascade define values */

#if DBT_BUILD_RELEASE // Release mode

#ifndef IN_HARDWARE_DEBUG
#define IN_HARDWARE_DEBUG 0
#endif
#ifndef ENABLE_TEXT_OUTPUT
#define ENABLE_TEXT_OUTPUT 0
#endif
#ifndef HAVE_RTT
#define HAVE_RTT 0
#endif

#else // Debug Mode

#ifndef IN_HARDWARE_DEBUG
#define IN_HARDWARE_DEBUG 1
#endif
#ifndef ENABLE_TEXT_OUTPUT
#define ENABLE_TEXT_OUTPUT 1
#endif
#ifndef HAVE_RTT
#define HAVE_RTT 1
#endif

#endif // if DBT_BUILD_RELEASE

#endif // ifndef DBT_H