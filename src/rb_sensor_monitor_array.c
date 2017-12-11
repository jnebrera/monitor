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

#include "rb_sensor_monitor_array.h"
#include "rb_sensor.h"

#include "utils.h"

#include <librd/rdfloat.h>
#include <librd/rdlog.h>

#define rb_monitors_array_new(count) rb_array_new(count)
#define rb_monitors_array_full(array) rb_array_full(array)
/** @note Doing with a function provides type safety */
static void
rb_monitors_array_add(rb_monitors_array_t *array, rb_monitor_t *monitor) {
	rb_array_add(array, monitor);
}

rb_monitor_t *rb_monitors_array_elm_at(rb_monitors_array_t *array, size_t i) {
	rb_monitor_t *ret = array->elms[i];
	return ret;
}

rb_monitors_array_t *parse_rb_monitors(json_object *monitors_array_json,
				       json_object *sensor_enrichment) {
	const size_t monitors_len =
			(size_t)json_object_array_length(monitors_array_json);
	rb_monitors_array_t *ret = rb_monitors_array_new(monitors_len);

	for (size_t i = 0; ret && i < monitors_len; ++i) {
		if (rb_monitors_array_full(ret)) {
			rdlog(LOG_CRIT,
			      "Sensors array full at %zu, can't add %zu",
			      ret->size,
			      i);
			break;
		}

		json_object *monitor_json = json_object_array_get_idx(
				monitors_array_json, i);
		rb_monitor_t *monitor = parse_rb_monitor(monitor_json,
							 sensor_enrichment);
		if (monitor) {
			rb_monitors_array_add(ret, monitor);
		}
	}

	return ret;
}

/** Process a monitor value
  @param monitor Monitor this monitor value is related
  @param monitor_value Monitor value to process
  @param ret Message list to report
  */
static void process_monitor_value(const rb_monitor_t *monitor,
				  const monitor_value *t_monitor_value,
				  rb_message_list *ret) {
	assert(monitor);
	assert(t_monitor_value);

	if (!rb_monitor_send(monitor)) {
		return;
	}

	rb_message_array_t *msgs =
			print_monitor_value(t_monitor_value, monitor);
	if (msgs) {
		rb_message_list_push(ret, msgs);
	}
}

bool process_monitors_array(rb_sensor_t *sensor,
			    rb_monitors_array_t *monitors,
			    ssize_t **monitors_deps,
			    rb_message_list *ret) {
	struct process_sensor_monitor_ctx *process_ctx = NULL;
	const size_t monitors_count = monitors->count;
	rb_monitor_value_array_t *monitor_values =
			rb_monitor_value_array_new(monitors_count);
	if (alloc_unlikely(!monitor_values)) {
		rdlog(LOG_ERR, "Couldn't allocate monitors values array");
		return false;
	}

	monitor_snmp_session *snmp_sess = rb_sensor_snmp_session(sensor);
	process_ctx = new_process_sensor_monitor_ctx(snmp_sess);

	for (size_t i = 0; i < monitors->count; ++i) {
		rb_monitor_value_array_t *op_vars =
				rb_monitor_value_array_select(monitor_values,
							      monitors_deps[i]);

		const rb_monitor_t *monitor =
				rb_monitors_array_elm_at(monitors, i);
		monitor_values->elms[i] = process_sensor_monitor(
				process_ctx, monitor, op_vars);
		if (likely(NULL != monitor_values->elms[i])) {
			process_monitor_value(
					monitor, monitor_values->elms[i], ret);
		}

		rb_monitor_value_array_done(op_vars);
	}

	if (process_ctx) {
		destroy_process_sensor_monitor_ctx(process_ctx);
	}
	for (size_t i = 0; i < monitors->count; ++i) {
		if (monitor_values->elms[i]) {
			rb_monitor_value_done(monitor_values->elms[i]);
		}
	}
	rb_monitor_value_array_done(monitor_values);

	return true;
}

/** Get a monitor position
  @param monitors_array Array of monitors
  @param name Name of monitor to find
  @return position of the monitor, or -1 if it couldn't be found
  */
static ssize_t
find_monitor_pos(const rb_monitors_array_t *monitors_array, const char *name) {
	for (size_t i = 0; i < monitors_array->count; ++i) {
		const char *i_name = rb_monitor_name(monitors_array->elms[i]);
		if (0 == strcmp(name, i_name)) {
			return (ssize_t)i;
		}
	}

	return -1;
}

/** Retuns a -1 terminated array with monitor operations variables position
  @param monitors_array Array of monitors
  @param monitor Monitor to search for
  @return requested array
  */
static ssize_t *
get_monitor_dependencies(const rb_monitors_array_t *monitors_array,
			 const rb_monitor_t *monitor) {
	ssize_t *ret = NULL;
	char **vars;
	size_t vars_len;

	rb_monitor_get_op_variables(monitor, &vars, &vars_len);

	if (vars_len > 0) {
		ret = calloc(vars_len + 1, sizeof(ret[0]));
		if (NULL == ret) {
			rdlog(LOG_ERR,
			      "Couldn't allocate dependencies array (OOM?)");
			goto err;
		}

		for (size_t i = 0; i < vars_len; ++i) {
			ret[i] = find_monitor_pos(monitors_array, vars[i]);
			if (-1 == ret[i]) {
				rdlog(LOG_ERR,
				      "Couldn't find variable [%s] in "
				      "operation [%s]. Discarding",
				      vars[i],
				      rb_monitor_get_cmd_data(monitor));
				free(ret);
				ret = NULL;
				goto err;
			}
		}

		ret[vars_len] = -1;
	}

err:
	rb_monitor_free_op_variables(vars, vars_len);
	return ret;
}

ssize_t **get_monitors_dependencies(const rb_monitors_array_t *monitors_array) {
	ssize_t **ret = calloc(monitors_array->count, sizeof(ret[0]));
	if (NULL == ret) {
		rdlog(LOG_ERR, "Couldn't allocate monitor dependences!");
		return NULL;
	}

	for (size_t i = 0; i < monitors_array->count; ++i) {
		const rb_monitor_t *i_monitor = monitors_array->elms[i];
		ret[i] = get_monitor_dependencies(monitors_array, i_monitor);
	}

	return ret;
}

void free_monitors_dependencies(ssize_t **deps, size_t count) {
	for (size_t i = 0; i < count; ++i) {
		free(deps[i]);
	}
	free(deps);
}

void rb_monitors_array_done(rb_monitors_array_t *monitors_array) {
	for (size_t i = 0; i < monitors_array->count; ++i) {
		rb_monitor_done(rb_monitors_array_elm_at(monitors_array, i));
	}
	free(monitors_array);
}
