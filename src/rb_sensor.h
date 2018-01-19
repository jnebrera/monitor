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

#include "rb_array.h"
#include "rb_message_list.h"
#include "rb_snmp.h"

#include <json-c/json.h>
#include <librd/rdqueue.h>
#include <librdkafka/rdkafka.h>

#include <stdbool.h>

typedef struct rb_sensor_s rb_sensor_t;

#ifdef NDEBUG
#define assert_rb_sensor(rb_sensor)
#else
void assert_rb_sensor(rb_sensor_t *sensor);
#endif

rb_sensor_t *parse_rb_sensor(/* const */ json_object *sensor_info);
bool process_rb_sensor(rb_sensor_t *sensor, rb_message_list *ret);

/** Obtains sensor name
  @param sensor Sensor
  @return Name of sensor.
  */
const char *rb_sensor_name(const rb_sensor_t *sensor);

/** Increase by 1 the reference counter for sensor
  @param sensor Sensor
  @todo this is not needed if we use proper enrichment
  */
void rb_sensor_get(rb_sensor_t *sensor);

/** Decrease the sensor reference counter.
  @param sensor Sensor
  */
void rb_sensor_put(rb_sensor_t *sensor);

/** Sensors array */
typedef struct rb_array rb_sensors_array_t;

/** Create a new array with count capacity */
#define rb_sensors_array_new(sz) rb_array_new(sz)

/** Destroy a sensors array */
void rb_sensors_array_done(rb_sensors_array_t *array);

/** Checks if a sensor array is full */
#define rb_sensors_array_full(array) rb_array_full(array)

/** Add a sensor to sensors array
  @note Wrapper function allows typechecking */
static void
rb_sensor_array_add(rb_sensors_array_t *array, rb_sensor_t *sensor) RD_UNUSED;
static void
rb_sensor_array_add(rb_sensors_array_t *array, rb_sensor_t *sensor) {
	rb_array_add(array, sensor);
}

/** Sensor snmp session */
monitor_snmp_session *rb_sensor_snmp_session(rb_sensor_t *sensor);
