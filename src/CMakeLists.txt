###########################################################################
# Copyright (C) 2015-2025 IoT.bzh Company
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

###########################################
# build and install afb-supervisor
###########################################

add_definitions(
	-DAFB_SUPERVISOR_VERSION="${PROJECT_VERSION}"
	-DAFB_SUPERVISOR_PORT=${AFB_SUPERVISOR_PORT}
)

add_compile_options(
	${json-c_CFLAGS}
	${libafb_CFLAGS}
	${libsystemd_CFLAGS}
)
add_executable(afb-supervisor afb-supervisor.c afb-supervisor-api.c afb-discover.c afb-supervisor-opts.c)

TARGET_LINK_LIBRARIES(afb-supervisor
	${json-c_LDFLAGS}
	${libafb_LDFLAGS}
	${libsystemd_LDFLAGS}
)

INSTALL(TARGETS afb-supervisor
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

CONFIGURE_FILE(afb-supervisor.service.in afb-supervisor.service @ONLY)
INSTALL(FILES
	${CMAKE_CURRENT_SOURCE_DIR}/afm-api-supervisor.service
	${CMAKE_CURRENT_SOURCE_DIR}/afm-api-supervisor.socket
	${CMAKE_CURRENT_BINARY_DIR}/afb-supervisor.service
	DESTINATION
	${UNITDIR_SYSTEM}
)


