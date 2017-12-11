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

#include <json-c/json.h>
#include <librd/rdmem.h>
#include <librd/rdtypes.h>

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>

typedef struct monitor_value {
#ifndef NDEBUG
#define MONITOR_VALUE_MAGIC 0x010AEA1C010AEA1CL
	uint64_t magic; // Private data, don't need to use them outside.
#endif

	/// Type of monitor value
	enum monitor_value_type {
		MONITOR_VALUE_T__BAD, ///< This is a bad array value
		MONITOR_VALUE_T__DOUBLE,
		MONITOR_VALUE_T__STRING,
		MONITOR_VALUE_T__ARRAY, ///< This is an array of monitors values
	} type;

	// Private data - Do not use directly

	/* response */
	union {
		struct {
			union {
				double value_d;
				struct {
					size_t size;
					char *buf;
				} value_s;
			};
		} value;
		struct {
			size_t children_count;
			struct monitor_value *split_op_result;
			struct monitor_value **children;
		} array;
	};
} monitor_value;

/// Returns the double value of monitor. If type is not a double, value is
/// converted.
double monitor_value_double(const struct monitor_value *mv);

/// Returns new monitor value
monitor_value *new_monitor_value_int(long i);
monitor_value *new_monitor_value_double(double dbl);

/// Creates a new monitor value in string format. The string value is borrowed.
monitor_value *new_monitor_value_strn(char *str, size_t str_len);

/// Creates a new string monitor value from fstream first line
monitor_value *new_monitor_value_fstream(FILE *stream);

// clang-format off
#define new_monitor_value(t_value)                                             \
	_Generic((t_value) + 0,                                                \
			 long: new_monitor_value_int,                          \
			 double: new_monitor_value_double)(t_value);
// clang-format on

/** Creates a new monitor value array from a string monitor value.
  @param mv Source and destination monitor value.
  @param split_tok Token to split string
  @param split_op Operation to do over result array ('sum' or 'mean')
 */
bool new_monitor_value_array_from_string(struct monitor_value *mv,
					 const char *split_tok,
					 const char *split_op);

/** Creates a new monitor value from children and split operation result */
struct monitor_value *
new_monitor_value_array(size_t children_count,
			struct monitor_value **children,
			struct monitor_value *split_op_result);

/// Casts a void pointer to an rb_monitor_value one.
#define rb_monitor_value_cast(t_mv)                                            \
	({                                                                     \
		union {                                                        \
			const struct monitor_value *mv;                        \
			typeof(t_mv) ret;                                      \
		} rb_monitor_value_cast = {.ret = t_mv};                       \
		assert(MONITOR_VALUE_MAGIC ==                                  \
		       rb_monitor_value_cast.mv->magic);                       \
		rb_monitor_value_cast.ret;                                     \
	})

void rb_monitor_value_done(struct monitor_value *mv);

/** Sensors array */
typedef struct rb_array rb_monitor_value_array_t;

/** Create a new array with count capacity */
#define rb_monitor_value_array_new(sz) rb_array_new(sz)

/** Destroy a sensors array */
#define rb_monitor_value_array_done(array) rb_array_done(array)

/** Checks if a monitor value array is full */
#define rb_monitor_value_array_full(array) rb_array_full(array)

/** Add a monitor value to monitor values array
  @note Wrapper function allows typechecking */
static void rb_monitor_value_array_add(rb_monitor_value_array_t *array,
				       struct monitor_value *mv) RD_UNUSED;
static void rb_monitor_value_array_add(rb_monitor_value_array_t *array,
				       struct monitor_value *mv) {
	rb_array_add(array, mv);
}

/** Select individual positions of original array
  @param array Original array
  @param pos list of positions (-1 terminated)
  @return New monitor array, that needs to be free with
	  rb_monitor_value_array_done
  @note Monitors are from original array, so they should not be touched
  */
rb_monitor_value_array_t *
rb_monitor_value_array_select(rb_monitor_value_array_t *array, ssize_t *pos);

/** Return monitor value of an array
  @param array Array
  @param i Position of array
  @return Monitor value at position i
  */
static struct monitor_value *
rb_monitor_value_array_at(rb_monitor_value_array_t *array, size_t i)
		__attribute__((unused));
static struct monitor_value *
rb_monitor_value_array_at(rb_monitor_value_array_t *array, size_t i) {
	return rb_monitor_value_cast(array->elms[i]);
}

/// @todo delete this FW declarations, print should not be here
struct rb_monitor_s;
struct rb_sensor_s;

/** Print a sensor value
  @param monitor_value Value to print
  @param monitor Value's monitor
  @return Message array with monitor value
  */
rb_message_array_t *
print_monitor_value(const struct monitor_value *monitor_value,
		    const struct rb_monitor_s *monitor);
