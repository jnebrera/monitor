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

static const char ops_monitor[] =  "{"
	"\"sensor_id\":1,"
	"\"timeout\":2,"
	"\"sensor_name\": \"sensor-arriba\","
	"\"sensor_ip\": \"localhost\","
	"\"community\" : \"public\","
	"\"monitors\": /* this field MUST be the last! */"
	"["
		"{\"name\": \"load_1\", \"system\":  \"echo 3\","
							"\"unit\": \"%\"},"
		"{\"name\": \"load_5\", \"system\": \"echo 2\","
							"\"unit\": \"%\"},"
		"// Operation\n"
		"{\"name\": \"100load_5\", \"op\":\"100*load_5\","
							"\"unit\": \"%\"},"
		"// 2 variables operation\n"
		"{\"name\": \"load_5_x_load_1\", \"op\":\"load_5*load_1\","
							"\"unit\": \"%\"},"

	"]"
	"}";

#define TEST1_CHECKS(mmonitor,mvalue,mtype)                                    \
	CHILD_I("sensor_id",1,                                                 \
	CHILD_S("sensor_name","sensor-arriba",                                 \
	CHILD_S("monitor",mmonitor,                                            \
	CHILD_S("value",mvalue,                                                \
	CHILD_S("type",mtype,                                                  \
	CHILD_S("unit","%",NULL))))))

static void prepare_math_ops_checks(check_list_t *check_list) {
	json_key_test checks[] = {
		JSON_KEY_TEST(TEST1_CHECKS("load_1","3.000000","system")),
		JSON_KEY_TEST(TEST1_CHECKS("load_5","2.000000","system")),
		JSON_KEY_TEST(TEST1_CHECKS("100load_5","200.000000","op")),
		JSON_KEY_TEST(TEST1_CHECKS("load_5_x_load_1","6.000000","op")),
	};

	check_list_push_checks(check_list, checks, RD_ARRAYSIZE(checks));
}

/** Basic test */
TEST_FN(test_ops, prepare_math_ops_checks, ops_monitor)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_ops),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
