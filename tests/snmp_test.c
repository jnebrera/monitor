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

#include "snmp_test.h"

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <librd/rd.h>

#include <setjmp.h>

#include <cmocka.h>

/**
 * @param type Type of response
 * @param oid Response oid
 * @param oid_len oid len
 * @param val Response value
 * @param val_size Response value size
 * @return New allocated PDU
 */
static struct snmp_pdu *snmp_sess_create_response(u_char type,
						  const oid *oid,
						  size_t oid_len,
						  const void *val,
						  size_t val_size) {
	struct snmp_pdu *response = calloc(1, sizeof(*response));
	response->variables =
			calloc(1, sizeof(*response->variables) + val_size);
	response->variables->type = type;
	// All val union members are pointers so we use one of them
	response->variables->val.objid = (void *)(&response->variables[1]);
	response->variables->val_len = val_size;
	memcpy(response->variables->val.objid, val, val_size);

	return response;
}

static int my_snmp_sess_synch_response(void *sessp,
				       struct snmp_pdu *pdu,
				       struct snmp_pdu **response) {
	static const long integers[] = {1, 2};
	static const char OID_3_STR[] = "3\n";
	static const oid EXPECTED_OID_PREFIX[] = {1, 3, 6, 1, 4, 1, 39483};

	(void)sessp;
	assert_true(pdu->variables[0].name_length =
				    RD_ARRAYSIZE(EXPECTED_OID_PREFIX) + 1);
	assert_true(0 == memcmp(EXPECTED_OID_PREFIX,
				pdu->variables[0].name,
				sizeof(EXPECTED_OID_PREFIX)));

	const size_t oid_suffix_pos = RD_ARRAYSIZE(EXPECTED_OID_PREFIX);
	const oid snmp_pdu_requested = pdu->variables[0].name[oid_suffix_pos];

#define SNMP_RES_CASE(res_oid_suffix, type, res_size, res)                     \
	case res_oid_suffix:                                                   \
		*response = snmp_sess_create_response(                         \
				type,                                          \
				pdu->variables[0].name,                        \
				pdu->variables[0].name_length,                 \
				res,                                           \
				res_size);                                     \
		snmp_free_pdu(pdu);                                            \
		return STAT_SUCCESS;

	switch (snmp_pdu_requested) {
		SNMP_RES_CASE(1, ASN_GAUGE, sizeof(integers[0]), &integers[0])
		SNMP_RES_CASE(2, ASN_INTEGER, sizeof(integers[1]), &integers[1])
		SNMP_RES_CASE(3, ASN_OCTET_STR, strlen(OID_3_STR), OID_3_STR)
	default:
		snmp_free_pdu(pdu);
		return STAT_ERROR;
		// @todo return STAT_TIMEOUT
	};
}

static void my_snmp_free_pdu(struct snmp_pdu *pdu) {
	free(pdu->variables);
	free(pdu);
}

bool wrap_snmp_funcs = false;
#define COMMA ,
#define WRAP_SNMP_FUN(ret_t, fun, args, call_args)                             \
	ret_t __real_##fun(args);                                              \
	ret_t __wrap_##fun(args) __attribute__((used));                        \
	ret_t __wrap_##fun(args) {                                             \
		return (*(wrap_snmp_funcs ? __real_##fun : my_##fun))(         \
				call_args);                                    \
	}

WRAP_SNMP_FUN(void, snmp_free_pdu, struct snmp_pdu *pdu, pdu)
WRAP_SNMP_FUN(int,
	      snmp_sess_synch_response,
	      void *sessp COMMA struct snmp_pdu *pdu
			      COMMA struct snmp_pdu **response,
	      sessp COMMA pdu COMMA response)
