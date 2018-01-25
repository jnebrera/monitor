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

#include "config.h"

#include <librdkafka/rdkafka.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <pthread.h>
#include <stdbool.h>

/// Trap handler
typedef struct trap_handler {
	const char *server_name; ///< Server to listen

/// private data - Do not use
#ifndef NDEBUG
#define TRAP_HANDLER_MAGIC 0xAA3A1CAA3A1CAA3A
	uint64_t magic;
#endif
	rd_kafka_topic_t *send_topic;
	netsnmp_transport *snmp_transport;
	pthread_t thread; ///< Associated thread
} trap_handler;

/** Init a trap handler for listen in a given port
  @param handler Traps handler
  @param server_name Name to listen into. Needs to be valid after call to this
function
  @warning NOT thread safe, need to be called before sessions stuff
*/
bool trap_handler_init(trap_handler *handler);

/// Delete trap handler
void trap_handler_done(trap_handler *handler);
