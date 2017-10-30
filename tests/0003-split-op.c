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

static const char split_op_sensor[] =  "{"
	"\"sensor_id\":1,"
	"\"timeout\":2,"
	"\"sensor_name\": \"sensor-arriba\","
	"\"sensor_ip\": \"localhost\","
	"\"community\" : \"public\","
	"\"monitors\": /* this field MUST be the last! */"
	"["
		/* TEST sum operation */
		"{\"name\": \"load_1\", \"system\": \"echo '3;2;1;0'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"sum\","
				"\"unit\": \"%\"},"
		/* TEST mean operation */
		"{\"name\": \"load_5\", \"system\": \"echo '4;5;6;7'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"unit\": \"%\"},"
		/* TEST do not send vector */
		"{\"name\": \"load_15\", \"system\": \"echo '8;9;10;11'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"sum\","
				"\"unit\": \"%\", \"send\":0},"
		/* TEST mean operation */
		"{\"name\": \"load_invalid\", \"system\": \"echo '4;5;6;7'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"invalid\","
				"\"unit\": \"%\"},"
	"]"
	"}";

#define TEST1_CHECKS(mmonitor,mvalue)                                          \
	CHILD_I("sensor_id",1,                                                 \
	CHILD_S("sensor_name","sensor-arriba",                                 \
	CHILD_S("monitor",mmonitor,                                            \
	CHILD_S("value",mvalue,                                                \
	CHILD_S("type","system",                                               \
	CHILD_S("unit","%",NULL))))))

#define TEST1_LOAD_1_CHECKS \
	JSON_KEY_TEST(TEST1_CHECKS("load_1_per_instance","3.000000")), \
	JSON_KEY_TEST(TEST1_CHECKS("load_1_per_instance","2.000000")), \
	JSON_KEY_TEST(TEST1_CHECKS("load_1_per_instance","1.000000")), \
	JSON_KEY_TEST(TEST1_CHECKS("load_1_per_instance","0.000000"))

#define TEST_LOAD_5(name) \
	JSON_KEY_TEST(TEST1_CHECKS(name,"4.000000")), \
	JSON_KEY_TEST(TEST1_CHECKS(name,"5.000000")), \
	JSON_KEY_TEST(TEST1_CHECKS(name,"6.000000")), \
	JSON_KEY_TEST(TEST1_CHECKS(name,"7.000000"))

#define TEST1_LOAD_5_CHECKS TEST_LOAD_5("load_5_per_instance")
#define TEST1_LOAD_INVALID_CHECKS TEST_LOAD_5("load_invalid_per_instance")

static void prepare_split_op_monitor_checks(check_list_t *check_list) {
	json_key_test checks[] = {
		TEST1_LOAD_1_CHECKS,
		JSON_KEY_TEST(TEST1_CHECKS("load_1","6.000000")),
		TEST1_LOAD_5_CHECKS,
		JSON_KEY_TEST(TEST1_CHECKS("load_5","5.500000")),
		TEST1_LOAD_INVALID_CHECKS,
	};

	check_list_push_checks(check_list, checks, RD_ARRAYSIZE(checks));
}

/** @TODO merge with previous test. Currently is impossible because a bad memctx
usage */
TEST_FN(test_split_op, prepare_split_op_monitor_checks, split_op_sensor)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_split_op),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
