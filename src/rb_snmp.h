/*
  Copyright (C) 2016 Eneo Tecnologia S.L.
  Copyright (C) 2017 Eugenio Perez
  Author: Eugenio Perez <eupm90@gmail.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "rb_value.h"

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <assert.h>
#include <stdbool.h>

/// Structure to be able to safely pass-around net-snmp pointer
typedef struct monitor_snmp_session {
	// Private data - Do not use
	void *sessp; ///< net-snmp session opaque pointer
} monitor_snmp_session;

/** Creates a new net-snmp session based on config
  @param ss SNMP Session
  @param params SNMP session parameters
  */
bool new_snmp_session(struct monitor_snmp_session *ss, netsnmp_session *params);

/**
  SNMP request & response adaption.
  @param oid_string String representing oid
  @param session    SNMP session to use
  @return           New monitor value
 */
monitor_value *snmp_solve_response(const char *oid_string,
				   struct monitor_snmp_session *session);

void destroy_snmp_session(struct monitor_snmp_session *);

int net_snmp_version(const char *string_version, const char *sensor_name);
