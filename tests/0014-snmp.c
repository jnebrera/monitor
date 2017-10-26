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

#include "config.h"

#include "json_test.h"
#include "sensor_test.h"
#include "snmp_test.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <setjmp.h> // Needs to be before of cmocka.h

#include <cmocka.h>

#include <stdarg.h>
#include <string.h>

static const uint16_t snmp_port = 161;

// clang-format off

static const char basic_sensor[] = "{\n"
	"\"sensor_id\":1,\n"
	"\"sensor_name\": \"sensor-arriba\",\n"
	"\"sensor_ip\": \"localhost\",\n"
	"\"community\" : \"public\",\n"
	"\"timeout\": 2,"
	"\"monitors\": /* this field MUST be the last! */\n"
	"[\n"
		"{\"name\": \"integer\", \"oid\":  \"1.3.6.1.4.1.39483.1\","
					" \"unit\": \"%\", \"send\": 1},\n"
		"{\"name\": \"gauge\", \"oid\": \"1.3.6.1.4.1.39483.2\","
					" \"unit\": \"%\", \"send\": 1},\n"
		"{\"name\": \"string\", \"oid\": \"1.3.6.1.4.1.39483.3\","
					"\"send\": 1},\n"
	"]\n"
	"}";

#define TEST1_CHECKS00(mmonitor,mvalue_type,mvalue)                            \
	CHILD_I("sensor_id",1,                                                 \
	CHILD_S("sensor_name","sensor-arriba",                                 \
	CHILD_S("monitor",mmonitor,                                            \
	mvalue_type("value",mvalue,                                            \
	CHILD_S("type","snmp",NULL)))))

#define TEST1_CHECKS0(mmonitor,mvalue_type,mvalue)                             \
	CHILD_S("unit","%",                                                    \
	TEST1_CHECKS00(mmonitor,mvalue_type,mvalue))

#define TEST1_CHECKS0_NOUNIT0(mmonitor,mvalue_type,mvalue)                     \
	TEST1_CHECKS00(mmonitor,mvalue_type,mvalue)

#define TEST1_CHECKS0_NOUNIT(mmonitor,mvalue) \
			TEST1_CHECKS0_NOUNIT0(mmonitor,CHILD_S,mvalue)

#define TEST1_CHECKS(mmonitor,mvalue) TEST1_CHECKS0(mmonitor,CHILD_S,mvalue)

static void prepare_test_basic_sensor_checks(check_list_t *check_list) {
	json_key_test checks[] = {
		JSON_KEY_TEST(TEST1_CHECKS("integer","1.000000")),
		JSON_KEY_TEST(TEST1_CHECKS("gauge","2.000000")),
		JSON_KEY_TEST(TEST1_CHECKS0_NOUNIT("string","3.000000")),
	};

	check_list_push_checks(check_list, checks, RD_ARRAYSIZE(checks));

}

/** Basic test */
TEST_FN(test_basic_sensor, prepare_test_basic_sensor_checks, basic_sensor)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_basic_sensor),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
