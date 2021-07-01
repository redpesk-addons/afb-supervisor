###########################################################################
# Copyright (C) 2015-2021 IoT.bzh Company
#
# Author: José Bollo <jose.bollo@iot.bzh>
#
# $RP_BEGIN_LICENSE$
# Commercial License Usage
#  Licensees holding valid commercial IoT.bzh licenses may use this file in
#  accordance with the commercial license agreement provided with the
#  Software or, alternatively, in accordance with the terms contained in
#  a written agreement between you and The IoT.bzh Company. For licensing terms
#  and conditions see https://www.iot.bzh/terms-conditions. For further
#  information use the contact form at https://www.iot.bzh/contact.
# 
# GNU General Public License Usage
#  Alternatively, this file may be used under the terms of the GNU General
#  Public license version 3. This license is as published by the Free Software
#  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
#  of this file. Please review the following information to ensure the GNU
#  General Public License requirements will be met
#  https://www.gnu.org/licenses/gpl-3.0.html.
# $RP_END_LICENSE$
###########################################################################

CMAKE_MINIMUM_REQUIRED(VERSION 3.0)

SET(PROJECT_NAME afb-supervisor)
SET(PROJECT_LANGUAGE C)
SET(PROJECT_PRETTY_NAME "Application Framework Supervisor")
SET(PROJECT_DESCRIPTION "Secured binder of API for clients of the Application framework")
set(PROJECT_VERSION 4.0.1)
set(PROJECT_URL "https://github.com/redpesk-core/afb-supervisor")

INCLUDE(FindPkgConfig)
INCLUDE(GNUInstallDirs)

###########################################################################

set(AFB_SUPERVISOR_PORT    1619 CACHE STRING "Port of service for the supervisor")
set(UNITDIR_SYSTEM         ${CMAKE_INSTALL_LIBDIR}/systemd/system CACHE STRING "Path to systemd system unit files")

###########################################################################

link_libraries(-Wl,--as-needed -Wl,--gc-sections -Wl,--no-undefined)

add_compile_options(-Wall -Wextra -Wconversion)
add_compile_options(-Wno-unused-parameter) # frankly not using a parameter does it care?
add_compile_options(-Wno-sign-compare -Wno-sign-conversion)
add_compile_options(
	$<$<C_COMPILER_ID:GNU>:-Werror=maybe-uninitialized>
	$<$<CXX_COMPILER_ID:GNU>:-Werror=maybe-uninitialized>
	$<$<C_COMPILER_ID:Clang>:-Werror=sometimes-uninitialized>
	$<$<CXX_COMPILER_ID:Clang>:-Werror=sometimes-uninitialized>
	$<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>
)
add_compile_options(-ffunction-sections -fdata-sections)
add_compile_options(-fPIC)
add_compile_options(-g -O2)

set (CMAKE_CXX_STANDARD 14)

set(CMAKE_C_FLAGS_PROFILING    "-g -O2 -pg -U_FORTIFY_SOURCE")
set(CMAKE_C_FLAGS_DEBUG        "-g -O2 -ggdb -U_FORTIFY_SOURCE")
set(CMAKE_C_FLAGS_RELEASE      "-g -O3")
set(CMAKE_C_FLAGS_COVERAGE     "-g -O2 --coverage")

###########################################################################

INCLUDE(FindThreads)
FIND_PACKAGE(Threads)

PKG_CHECK_MODULES(json-c REQUIRED json-c)
PKG_CHECK_MODULES(libsystemd REQUIRED libsystemd>=222)
PKG_CHECK_MODULES(libafb REQUIRED libafb>=4.0)

ADD_SUBDIRECTORY(src)

