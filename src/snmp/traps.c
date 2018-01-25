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

#include "rb_sensor_monitor.h"
#include "rb_snmp.h"

#include "utils.h"

#include <librd/rd.h>
#include <librd/rdlog.h>

#ifdef TRAP_HANDLER_MAGIC
#define trap_handler_cast(void_ptr)                                            \
	({                                                                     \
		union {                                                        \
			const trap_handler *cast;                              \
			typeof(void_ptr) ptr;                                  \
		} trap_handler_cast = {.cast = (void_ptr)};                    \
		assert(TRAP_HANDLER_MAGIC == trap_handler_cast.cast->magic);   \
		trap_handler_cast.ptr;                                         \
	})
#else
#define trap_handler_cast(trap_handler) (trap_handler)
#endif

/** Transforms snmp v1 traps oid in v2c/v3 format
  @param pdu PDU to extract oid
  @param trap_oid OID to save. It's assumed to be MAX_OID_LEN + 2 size.
  @param trap_oid_len length for save trap_oid.
  @todo Test extreme long oid attack
  @return new trap_oid_len
  */
static size_t extract_snmpv1_trap_oid(const netsnmp_pdu *pdu, oid trap_oid[]) {
	static const oid std_trap_oid_root[] = {1, 3, 6, 1, 6, 3, 1, 1, 5};

	size_t trap_oid_len = 0;
	if (pdu->trap_type == SNMP_TRAP_ENTERPRISESPECIFIC) {
		trap_oid_len = pdu->enterprise_length;
		memcpy(trap_oid, pdu->enterprise, sizeof(oid) * trap_oid_len);
		if (trap_oid[trap_oid_len - 1] != 0) {
			trap_oid[trap_oid_len++] = 0;
		}
		trap_oid[trap_oid_len++] = (long unsigned)pdu->specific_type;
	} else {
		memcpy(trap_oid, std_trap_oid_root, sizeof(std_trap_oid_root));
		trap_oid_len = OID_LENGTH(std_trap_oid_root); /* 9 */
		trap_oid[trap_oid_len++] = (long unsigned)pdu->trap_type + 1;
	}
	return trap_oid_len;
}

static json_object *
snmp_var_enrichment_variable(const netsnmp_variable_list *var) {
	// @todo See in /usr/include/net-snmp/types.h
	switch (var->type) {
	case ASN_GAUGE:
	case ASN_INTEGER:
		return json_object_new_int64(*var->val.integer);
	case ASN_OCTET_STR:
		if (unlikely(var->val_len == 0)) {
			// No return at all
			return NULL;
		}

		return json_object_new_string_len((const char *)var->val.string,
						  var->val_len);
	case ASN_NULL:
		break;
	default:
		rdlog(LOG_WARNING,
		      "Unknow variable type %d in SNMP response",
		      var->type);
		break;
	};

	return NULL;
}

static char *snmp_oid_name(const oid *objid, size_t objidlen) {
	static const int allow_realloc = 1;
	unsigned char *ret = NULL;
	sprint_realloc_objid(&ret,
			     (size_t[]){0},
			     (size_t[]){0},
			     allow_realloc,
			     objid,
			     objidlen);
	return (char *)ret;
}

static char *snmp_variable_name(const netsnmp_variable_list *var) {
	return snmp_oid_name(var->name, var->name_length);
}

/** Add an enrichment field to enrichment object.
  @param enrichment Base enrichment object. Can be NULL, and it will be
	 allocated.
  @param key Enrichment key
  @param enrichment_child Enrichment child to add with key.
  @return (Possibly) new allocated object. If NULL, an error occurred and
	  enrichment parameter is still valid. If !NULL, this is the new
	  enrichment you have to use.
  */
static json_object *add_enrichment_object(json_object *enrichment,
					  const char *key,
					  json_object *enrichment_child) {
	if (!enrichment) {
		enrichment = json_object_new_object();
		if (alloc_unlikely(NULL == enrichment)) {
			rdlog(LOG_ERR, "Couldn't allocate enrichment");
			return NULL;
		}
	}

	json_object_object_add(enrichment, key, enrichment_child);
	return enrichment;
}

static json_object *
add_snmp_var_monitor_enrichment(const netsnmp_variable_list *var,
				json_object *enrichment) {
	char *enrichment_name = snmp_variable_name(var);
	if (unlikely(NULL == enrichment_name)) {
		/// @TODO sanitize this
		char oid_str[4 * sizeof(oid) * var->name_length];
		snprint_objid(oid_str,
			      sizeof(oid_str),
			      var->name,
			      var->name_length);
		rdlog(LOG_ERR,
		      "Couldn't get enrichment name of %s oid",
		      oid_str);
		goto err;
	}

	json_object *child = snmp_var_enrichment_variable(var);
	if (unlikely(NULL == child)) {
		goto err;
	}

	json_object_object_add(enrichment, enrichment_name, child);

err:
	free(enrichment_name);
	return enrichment;
}

/** Special treatment for interface index treatment.
  Link up / link down special treatment for interfaces. SNMP will send the
  affected interface index using first OID in variables, and a null value in
  that. We will transform it to a more treatable way:
  {"ifIndex.1":null} -> {"ifIndex":1}
  @param snmp_if_index_oid SNMP interface index oid
  @param snmp_if_index_len SNMP interface index oid length
  @param var Variable that contains interface id.
  @param enrichment Current monitor enrichment
  @return New monitor enrichment
*/
static json_object *
snmp_trap_interface_index_treatment(size_t snmp_if_index_len,
				    const netsnmp_variable_list *var,
				    json_object *enrichment) {
	assert(var->name_length > snmp_if_index_len);

	const size_t oid_suffix_len = var->name_length - snmp_if_index_len;
	// Assuming char = 1 byte <= strlen("255.")
	// TODO sanitize this.
	char oid_buf[4 * sizeof(oid) * oid_suffix_len + 1];
	char *cursor = oid_buf;
	for (size_t i = 0; i < oid_suffix_len; ++i) {
		assert(sizeof(oid_buf) > (size_t)(cursor - oid_buf));
		const size_t remaining =
				sizeof(oid_buf) - (size_t)(cursor - oid_buf);
		cursor += snprintf(cursor,
				   remaining,
				   "%" NETSNMP_PRIo "u",
				   var->name[snmp_if_index_len + i]);
	}

	json_object *if_index_json =
			json_object_new_string_len(oid_buf, cursor - oid_buf);
	if (alloc_unlikely(NULL == if_index_json)) {
		rdlog(LOG_ERR,
		      "Couldn't allocate child interface enrichment (OOM?)");
		return NULL;
	}

	json_object *new_enrichment = add_enrichment_object(
			enrichment, "if_index", if_index_json);
	if (unlikely(NULL == new_enrichment)) {
		json_object_put(if_index_json);
	}

	return new_enrichment;
}

static void send_snmp_trap_pdu(trap_handler *this, const netsnmp_pdu *pdu) {
	static const oid snmp_trap_oid[] = {1, 3, 6, 1, 6, 3, 1, 1, 4, 1, 0};
	static const oid snmp_uptime_oid[] = {1, 3, 6, 1, 2, 1, 1, 3, 0};
	static const oid snmp_if_index_oid[] = {1, 3, 6, 1, 2, 1, 2, 2, 1, 1};
	/// @TODO check for netsnmp_pdu transport data, in order to localize
	/// sensor & make enrichment
	json_object *enrichment = NULL;
	char *monitor_name = NULL;
	monitor_value *trap_value = new_monitor_value(1l);
	rb_monitor_t *monitor = NULL;
	rb_message_array_t *send_array = NULL;
	if (alloc_unlikely(NULL == trap_value)) {
		return;
	}

	// clang-format off
	char *sensor_addr =
		this->snmp_transport->f_fmtaddr ?
		this->snmp_transport->f_fmtaddr(this->snmp_transport,
						pdu->transport_data,
						pdu->transport_data_length)
				: NULL;
	// clang-format on
	if (sensor_addr) {
		json_object *sensor_name = json_object_new_string(sensor_addr);
		enrichment = add_enrichment_object(
				enrichment, "sensor_name", sensor_name);
	}

	if (pdu->command == SNMP_MSG_TRAP) {
		oid trap_oid[MAX_OID_LEN + 2] = {0};
		size_t trap_oid_len = 0;
		trap_oid_len = extract_snmpv1_trap_oid(pdu, trap_oid);
		monitor_name = snmp_oid_name(trap_oid, trap_oid_len);
	}

	for (const netsnmp_variable_list *var = pdu->variables; var;
	     var = var->next_variable) {
		// @TODO use this as monitor value timestamp!!
		if (!snmp_oid_compare(var->name,
				      OID_LENGTH(snmp_uptime_oid),
				      snmp_uptime_oid,
				      OID_LENGTH(snmp_uptime_oid))) {
			continue;
		}

		if (!snmp_oid_compare(var->name,
				      OID_LENGTH(snmp_trap_oid),
				      snmp_trap_oid,
				      OID_LENGTH(snmp_trap_oid))) {
			monitor_name = snmp_oid_name(
					var->val.objid,
					var->val_len / sizeof(oid));
			continue;
		}

		if (!snmp_oid_compare(var->name,
				      OID_LENGTH(snmp_if_index_oid),
				      snmp_if_index_oid,
				      OID_LENGTH(snmp_if_index_oid))) {

			json_object *new_enrichment =
					snmp_trap_interface_index_treatment(
							OID_LENGTH(snmp_if_index_oid),
							var,
							enrichment);
			if (likely(NULL != new_enrichment)) {
				enrichment = new_enrichment;
				continue;
			}
			// else, try to print the regular way
		}

		json_object *new_enrichment = add_snmp_var_monitor_enrichment(
				var, enrichment);
		if (new_enrichment) {
			enrichment = new_enrichment;
		}
	}

	if (unlikely(NULL == monitor_name)) {
		rdlog(LOG_ERR, "Can't find trap OID");
		goto err;
	}

	monitor = create_snmp_trap_rb_monitor(monitor_name, enrichment);
	if (alloc_unlikely(NULL == monitor)) {
		rdlog(LOG_ERR, "Couldn't create monitor (OOM?)");
		goto err;
	}

	send_array = print_monitor_value(trap_value, monitor);
	if (alloc_unlikely(NULL == send_array)) {
		rdlog(LOG_ERR, "Couldn't create send array (OOM?)");
		goto err;
	}

	/// @TODO abstract the kafka production in its own module
	int msgs_ok = rd_kafka_produce_batch(this->send_topic,
					     RD_KAFKA_PARTITION_UA,
					     RD_KAFKA_MSG_F_FREE,
					     send_array->msgs,
					     send_array->count);

	// Check for all failures
	for (size_t i = 0;
	     (size_t)msgs_ok < send_array->count && i < send_array->count;
	     ++i) {
		if (send_array->msgs[i].err) {
			rdlog(LOG_ERR,
			      "Couldn't send message [%.*s]",
			      (int)send_array->msgs[i].len,
			      (char *)send_array->msgs[i].payload);
			free(send_array->msgs[i].payload);
			msgs_ok++;
		}
	}
err:
	if (likely(NULL != send_array)) {
		message_array_done(send_array);
	}
	if (likely(NULL != monitor)) {
		rb_monitor_done(monitor);
	}

	free(monitor_name);

	if (likely(NULL != trap_value)) {
		rb_monitor_value_done(trap_value);
	}
	if (likely(NULL != enrichment)) {
		json_object_put(enrichment);
	}
}

/// @note Copied from net-snmp 5.7.3 snmptrapd_handlers.c
static int snmp_trap_callback(int op,
			      netsnmp_session *session,
			      int reqid,
			      netsnmp_pdu *pdu,
			      void *magic) {
	(void)reqid;
	trap_handler *handler = trap_handler_cast(magic);

	if (NETSNMP_CALLBACK_OP_RECEIVED_MESSAGE != op) {
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
	} else if (unlikely(pdu->command != SNMP_MSG_TRAP2 &&
			    pdu->command != SNMP_MSG_TRAP)) {
		/* @TODO handle error */
		return 1; /* ??? */
	}

	send_snmp_trap_pdu(handler, pdu);
	return 0;
}

static netsnmp_session *
snmptrapd_add_session(trap_handler *this, netsnmp_transport *t) {
	netsnmp_session sess, *rc = NULL;

	snmp_sess_init(&sess);
	sess.peername = SNMP_DEFAULT_PEERNAME;
	sess.version = SNMP_DEFAULT_VERSION;
	sess.community_len = SNMP_DEFAULT_COMMUNITY_LEN;
	sess.retries = SNMP_DEFAULT_RETRIES;
	sess.timeout = SNMP_DEFAULT_TIMEOUT;
	sess.callback = snmp_trap_callback;
	sess.callback_magic = this;
	sess.isAuthoritative = SNMP_SESS_UNKNOWNAUTH;

	rc = snmp_sess_add(&sess, t, NULL, NULL);
	if (rc == NULL) {
		snmp_sess_perror("snmptrapd", snmp_sess_session(rc));
	}
	return rc;
}

/// Open snmp traps server
static void *open_server(trap_handler *this, const char *server_name) {
	this->snmp_transport =
			netsnmp_transport_open_server("monitor", server_name);
	if (unlikely(this->snmp_transport == NULL)) {
		snmp_log(LOG_ERR,
			 "couldn't open %s -- errno %d (\"%s\")\n",
			 server_name,
			 errno,
			 gnu_strerror_r(errno));
		return NULL;
	}

	void *ss = snmptrapd_add_session(this, this->snmp_transport);
	if (unlikely(ss == NULL)) {
		/*
		 * Shouldn't happen?  We have already opened the
		 * transport successfully so what could have gone wrong?
		 */
		snmp_log(LOG_ERR,
			 "couldn't open snmp - %s",
			 gnu_strerror_r(errno));
		goto add_sess_err;
	}

	return ss;

add_sess_err:
	netsnmp_transport_free(this->snmp_transport);
	return NULL;
}

static void traps_dispatch(void *snmp_sess) {
	int numfds = 0, block = 0;
	fd_set readfds, writefds, exceptfds;
	struct timeval timeout = {.tv_sec = 5};

	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);

	rdlog(LOG_DEBUG, "Asking NET-SNMP for file descriptors to select");
	const int select_info_rc = snmp_sess_select_info(
			snmp_sess, &numfds, &readfds, &timeout, &block);
	rdlog(LOG_DEBUG, "select block: %d", select_info_rc);
	if (0 == select_info_rc) {
		rdlog(LOG_DEBUG, "No file descriptors to monitor");
		return;
	}

	const int count = select(numfds,
				 &readfds,
				 &writefds,
				 &exceptfds,
				 block ? NULL : &timeout);
	rdlog(LOG_DEBUG, "Select rc: %d", count);
	if (unlikely(count < 0)) {
		if (errno == EINTR) {
			return;
		}
		snmp_sess_perror("select error", snmp_sess);
	} else {
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, (int[]){0});
		if (count > 0) {
			/* If there are any more events after external events,
			 * then try SNMP events. */
			snmp_sess_read(snmp_sess, &readfds);
		} else {
			snmp_sess_timeout(snmp_sess);
		}
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, (int[]){0});
	}
}

static void pthread_sess_close_cb(void *sessp) {
	snmp_sess_close(sessp);
}

static void *handler_thread_callback(void *vtrap_handler) {
	trap_handler *this = trap_handler_cast(vtrap_handler);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, (int[]){0});
	// Musl doesn't cancel a blocking select
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	netsnmp_session *trap_session = open_server(this, this->server_name);
	if (unlikely(NULL == trap_session)) {
		return NULL;
	}

	rdlog(LOG_INFO, "Listening for traps on %s", this->server_name);

	pthread_cleanup_push(pthread_sess_close_cb, trap_session);

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, (int[]){0});
	while (1) {
		traps_dispatch(trap_session);
	}
	pthread_cleanup_pop(1);

	return NULL;
}

bool trap_handler_init(trap_handler *this) {
	static const pthread_attr_t *thread_attr = NULL;

#ifdef TRAP_HANDLER_MAGIC
	this->magic = TRAP_HANDLER_MAGIC;
#endif

	const int create_rc = pthread_create(&this->thread,
					     thread_attr,
					     handler_thread_callback,
					     this);
	if (unlikely(create_rc != 0)) {
		rdlog(LOG_ERR,
		      "Couldn't create trap thread: %s",
		      gnu_strerror_r(errno));
	}

	return !create_rc;
}

/// Delete trap handler
void trap_handler_done(trap_handler *this) {
	pthread_cancel(this->thread);
	pthread_join(this->thread, NULL);
}
