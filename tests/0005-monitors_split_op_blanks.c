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

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <setjmp.h> // Needs to be before of cmocka.h

#include <cmocka.h>

#include <stdarg.h>
#include <string.h>

// clang-format off

static const char split_op_sensor_blanks[] =  "{"
	"\"sensor_id\":1,"
	"\"timeout\":2,"
	"\"sensor_name\": \"sensor-arriba\","
	"\"sensor_ip\": \"localhost\","
	"\"community\" : \"public\","
	"\"monitors\": /* this field MUST be the last! */"
	"["
		"{\"name\": \"load_1\", \"system\": \"echo ';2;1;0'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"sum\","
				"\"instance_prefix\":\"load-\",\"send\":0,"
				"\"unit\": \"%\"},"
		"{\"name\": \"load_5\", \"system\": \"echo ';6;8;10'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"load-\",\"send\":0,"
				"\"unit\": \"%\"},"
		"{\"name\": \"load_1+5\", \"op\": \"load_1+load_5\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"load-instance-\","
				"\"unit\": \"%\"},"

		"{\"name\": \"v1\", \"system\": \"echo ';12;11;10'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"sum\","
				"\"instance_prefix\":\"load-\",\"send\":0,"
				"\"unit\": \"%\"},"
		"{\"name\": \"v2\", \"system\": \"echo '14;16;;10'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"load-\",\"send\":0,"
				"\"unit\": \"%\"},"
		"{\"name\": \"v1+v2\", \"op\": \"v1+v2\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"load-instance-\","
				"\"unit\": \"%\"},"
	"]"
	"}";

#define TEST1_CHECKS0_V(mmonitor,mvalue)                                       \
	CHILD_I("sensor_id",1,                                                 \
	CHILD_S("sensor_name","sensor-arriba",                                 \
	CHILD_S("monitor",mmonitor,                                            \
	CHILD_S("value",mvalue,                                                \
	CHILD_S("type","op",                                                   \
	CHILD_S("unit","%", NULL))))))

#define TEST1_CHECKS0_I(mmonitor,mvalue,minstance)                             \
	JSON_KEY_TEST(                                                         \
		CHILD_S("instance",minstance,TEST1_CHECKS0_V(mmonitor,mvalue)))

#define TEST1_CHECKS0_AVG(mmonitor,mvalue)                                     \
	JSON_KEY_TEST(TEST1_CHECKS0_V(mmonitor,mvalue))

static void prepare_split_op_monitor_blanks_checks(check_list_t *check_list) {
	json_key_test checks_v[] = {
		TEST1_CHECKS0_I("load_1+5_per_instance","8.000000",
							"load-instance-1"),
		TEST1_CHECKS0_I("load_1+5_per_instance","9.000000",
							"load-instance-2"),
		TEST1_CHECKS0_I("load_1+5_per_instance","10.000000",
							"load-instance-3")
	};

	json_key_test checks_op[] = {
		TEST1_CHECKS0_AVG("load_1+5", "9.000000")
	};

	check_list_push_checks(check_list, checks_v, RD_ARRAYSIZE(checks_v));
	check_list_push_checks(check_list, checks_op, 1);

	json_key_test checks_vars[] = {
		TEST1_CHECKS0_I("v1+v2_per_instance","14.000000",
							"load-instance-0"),
		TEST1_CHECKS0_I("v1+v2_per_instance","28.000000",
							"load-instance-1"),
		TEST1_CHECKS0_I("v1+v2_per_instance","11.000000",
							"load-instance-2"),
		TEST1_CHECKS0_I("v1+v2_per_instance","20.000000",
							"load-instance-3")
	};

	json_key_test checks_vars_op[] = {
		TEST1_CHECKS0_AVG("v1+v2", "18.250000")
	};

	check_list_push_checks(check_list, checks_vars,
						RD_ARRAYSIZE(checks_vars));
	check_list_push_checks(check_list, checks_vars_op, 1);
}

/** Test with blanks in instances */
TEST_FN(test_split_op_blanks, prepare_split_op_monitor_blanks_checks,
							split_op_sensor_blanks)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_split_op_blanks),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
