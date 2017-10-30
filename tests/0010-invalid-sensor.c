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

#include "rb_sensor.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <setjmp.h> // Needs to be before of cmocka.h

#include <cmocka.h>

#include <stdarg.h>
#include <string.h>

// clang-format off

static const char *invalid_sensors[] = {
		/* No monitors */
		"{\n"
			"\"sensor_id\":1,\n"
			"\"timeout\":100000000000000000000000,\n"
			"\"sensor_name\": \"sensor-arriba\",\n"
			"\"sensor_ip\": \"localhost\",\n"
			"\"community\" : \"public\",\n"
		"}",
		/* No name */
		"{\n"
			"\"sensor_id\":1,\n"
			"\"timeout\":100000000000000000000000,\n"
			"\"sensor_ip\": \"localhost\",\n"
			"\"community\" : \"public\",\n"
			"\"monitors\": /* this field MUST be the last! */"
			"["
				"{\"name\": \"load_1\", "
				"\"system\":  \"echo 1\","
				"\"unit\": \"%\", \"send\": 0},"
			"]"
		"}",
		/* No peer ip */
		"{\n"
			"\"sensor_name\": \"sensor-arriba\",\n"
			"\"sensor_id\":1,\n"
			"\"timeout\":100000000000000000000000,\n"
			"\"community\" : \"public\",\n"
			"\"monitors\": /* this field MUST be the last! */"
			"["
				"{\"name\": \"load_1\", "
				"\"system\":  \"echo 1\","
				"\"unit\": \"%\", \"send\": 0},"
			"]"
		"}",
		/* No community */
		"{\n"
			"\"sensor_name\": \"sensor-arriba\",\n"
			"\"sensor_id\":1,\n"
			"\"timeout\":100000000000000000000000,\n"
			"\"sensor_ip\": \"localhost\",\n"
			"\"monitors\": /* this field MUST be the last! */"
			"["
				"{\"name\": \"load_1\", "
				"\"system\":  \"echo 1\","
				"\"unit\": \"%\", \"send\": 0},"
			"]"
		"}",
	};

void test_invalid_sensors() {
	struct _worker_info worker_info;
        memset(&worker_info, 0, sizeof(worker_info));
        snmp_sess_init(&worker_info.default_session);


	for (size_t i=0; i<RD_ARRAYSIZE(invalid_sensors); ++i) {
		const char *text_sensor = invalid_sensors[i];
	        struct json_object *json_sensor = json_tokener_parse(text_sensor);
	        rb_sensor_t *sensor = parse_rb_sensor(json_sensor, &worker_info);
	        assert_null(sensor);
	        json_object_put(json_sensor);
	}
}

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_invalid_sensors),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
