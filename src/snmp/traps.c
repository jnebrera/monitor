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

#include "traps.h"

#include "utils.h"

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <librd/rd.h>
#include <librd/rdlog.h>

#ifdef TRAP_HANDLER_MAGIC
#define trap_handler_cast(void_ptr)                                            \
	({                                                                     \
		const struct {                                                 \
			const trap_handler *cast;                              \
			typeof(void_ptr) ptr,                                  \
		} trap_handler_cast;                                           \
		trap_handler_cast.ptr = (void_ptr); /* avoid side effects */   \
		trap_handler_cast.cast = trap_handler_cast.ptr;                \
		assert(TRAP_HANDLER_MAGIC == trap_handler_cast.cast->magic);   \
		void_ptr;                                                      \
	})
#else
#define trap_handler_cast(trap_handler) (trap_handler)
#endif

/// @note Copied from net-snmp 5.7.3 snmptrapd_handlers.c
static int snmp_trap_callback(int op,
			      netsnmp_session *session,
			      int reqid,
			      netsnmp_pdu *pdu,
			      void *magic) {
	static const oid stdTrapOidRoot[] = {1, 3, 6, 1, 6, 3, 1, 1, 5};
	static const oid snmpTrapOid[] = {1, 3, 6, 1, 6, 3, 1, 1, 4, 1, 0};
	oid trapOid[MAX_OID_LEN + 2] = {0};
	int trapOidLen;
	(void)magic;

	if (unlikely(NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE != op)) {
		switch (op) {
		case NETSNMP_CALLBACK_OP_TIMED_OUT:
		case NETSNMP_CALLBACK_OP_SEND_FAILED:
		case NETSNMP_CALLBACK_OP_CONNECT:
		case NETSNMP_CALLBACK_OP_DISCONNECT:
			/* Ignore silently */
			return 0;

		default:
			rdlog(LOG_ERR, "Unknown operation (%d)", op);
			return 0;
		};
	}

	if (unlikely(session->s_snmp_errno)) {
		rdlog(LOG_ERR,
		      "SNMP session error %d: %s",
		      session->s_snmp_errno,
		      gnu_strerror_r(session->s_snmp_errno));
		return 1;
	}

	switch (pdu->command) {
	case SNMP_MSG_TRAP:
		/*
		 * Convert v1 traps into a v2-style trap OID
		 *    (following RFC 2576)
		 */
		if (pdu->trap_type == SNMP_TRAP_ENTERPRISESPECIFIC) {
			trapOidLen = pdu->enterprise_length;
			memcpy(trapOid,
			       pdu->enterprise,
			       sizeof(oid) * trapOidLen);
			if (trapOid[trapOidLen - 1] != 0) {
				trapOid[trapOidLen++] = 0;
			}
			trapOid[trapOidLen++] = pdu->specific_type;
		} else {
			memcpy(trapOid, stdTrapOidRoot, sizeof(stdTrapOidRoot));
			trapOidLen = OID_LENGTH(stdTrapOidRoot); /* 9 */
			trapOid[trapOidLen++] = pdu->trap_type + 1;
		}
		break;

	case SNMP_MSG_TRAP2:
	case SNMP_MSG_INFORM:
		/*
		 * v2c/v3 notifications *should* have snmpTrapOID as the
		 *    second varbind, so we can go straight there.
		 *    But check, just to make sure
		 */
		netsnmp_variable_list *vars = pdu->variables;
		if (likely(vars)) {
			vars = vars->next_variable;
		}
		if (unlikely(!vars ||
			     snmp_oid_compare(vars->name,
					      vars->name_length,
					      snmpTrapOid,
					      OID_LENGTH(snmpTrapOid)))) {
			/*
			 * Didn't find it!
			 * Let's look through the full list....
			 */
			for (vars = pdu->variables; vars;
			     vars = vars->next_variable) {
				if (!snmp_oid_compare(vars->name,
						      vars->name_length,
						      snmpTrapOid,
						      OID_LENGTH(snmpTrapOid)))
					break;
			}
			if (!vars) {
				/*
				 * Still can't find it!  Give up.
				 */
				rdlog(LOG_ERR,
				      "Cannot find TrapOID in TRAP2 PDU");
				return 1; /* ??? */
			}
		}
		memcpy(trapOid, vars->val.objid, vars->val_len);
		trapOidLen = vars->val_len / sizeof(oid);
		break;

	default:
		/* SHOULDN'T HAPPEN! */
		return 1; /* ??? */
	};

	if (pdu->command == SNMP_MSG_INFORM) {
		netsnmp_pdu *reply = snmp_clone_pdu(pdu);
		if (alloc_unlikely(!reply)) {
			rdlog(LOG_ERR,
			      "couldn't clone PDU for INFORM "
			      "response\n");
		} else {
			reply->command = SNMP_MSG_RESPONSE;
			reply->errstat = 0;
			reply->errindex = 0;
			if (unlikely(!snmp_send(session, reply))) {
				snmp_sess_perror("snmptrapd: Couldn't respond "
						 "to inform pdu",
						 session);
				snmp_free_pdu(reply);
			}
		}
	}

	return 0;
}

static netsnmp_session *snmptrapd_add_session(netsnmp_transport *t) {
	netsnmp_session sess, *session = &sess, *rc = NULL;

	snmp_sess_init(session);
	session->peername = SNMP_DEFAULT_PEERNAME;
	session->version = SNMP_DEFAULT_VERSION;
	session->community_len = SNMP_DEFAULT_COMMUNITY_LEN;
	session->retries = SNMP_DEFAULT_RETRIES;
	session->timeout = SNMP_DEFAULT_TIMEOUT;
	session->callback = snmp_input;
	session->callback_magic = (void *)t;
	session->authenticator = NULL;
	sess.isAuthoritative = SNMP_SESS_UNKNOWNAUTH;

	rc = snmp_add(session, t, pre_parse, NULL);
	if (rc == NULL) {
		snmp_sess_perror("snmptrapd", session);
	}
	return rc;
}

/// Open snmp traps server
static netsnmp_session *open_server(const char *server_name) {
	netsnmp_transport *transport =
			netsnmp_transport_open_server("monitor", server_name);
	if (transport == NULL) {
		snmp_log(LOG_ERR,
			 "couldn't open %s -- errno %d (\"%s\")\n",
			 server_name,
			 errno,
			 strerror(errno));
		return NULL;
	}

	netsnmp_session *ss = snmptrapd_add_session(transport);
	if (ss == NULL) {
		/*
		 * Shouldn't happen?  We have already opened the
		 * transport successfully so what could have gone wrong?
		 */
		snmp_log(LOG_ERR, "couldn't open snmp - %s", strerror(errno));
		goto add_sess_err;
	}

	return ss;

add_sess_err:
	netsnmp_transport_free(transport);
	return NULL;
}

static void *handler_thread_callback(void *vtrap_handler) {
	trap_handler *handler = trap_handler_cast(vtrap_handler);
	netsnmp_session *trap_session = open_server(handler->server_name);
	return NULL;
}

int trap_handler_init(trap_handler *handler, const char *server_name) {
	static const const pthread_attr_t *thread_attr = NULL;
	handler->server_name = server_name;

	const int create_rc = pthread_create(&handler->thread,
					     thread_attr,
					     handler_thread_callback,
					     handler);
	if (unlikely(create_rc != 0)) {
		rdlog(LOG_ERR,
		      "Couldn't create trap thread: %s",
		      gnu_strerror_r(errno));
	}

	return create_rc;
}

/// Delete trap handler
void trap_handler_done(trap_handler *handler) {
	pthread_cancel(handler->thread);
}
