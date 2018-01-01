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

#include "utils.h"

#include "rb_sensor_monitor.h"

#include "rb_sensor.h"

#include "poller/system.h"
#include "rb_libmatheval.h"
#include "rb_snmp.h"

#include "rb_json.h"

#include <librd/rdfloat.h>
#include <librd/rdlog.h>

#include <math.h>
#include <matheval.h>

/// X-macro to define monitor operations
/// _X(menum,cmd,value_type,fn)
#define MONITOR_CMDS_X                                                         \
	/* Will launch a shell and execute given command, capturing output */  \
	_X(RB_MONITOR_T__SYSTEM,                                               \
	   "system",                                                           \
	   "system",                                                           \
	   rb_monitor_get_system_external_value)                               \
	/* Will ask SNMP server for a given oid */                             \
	_X(RB_MONITOR_T__OID,                                                  \
	   "oid",                                                              \
	   "snmp",                                                             \
	   rb_monitor_get_snmp_external_value)                                 \
	/* Will operate over previous results */                               \
	_X(RB_MONITOR_T__OP, "op", "op", rb_monitor_get_op_result)

struct rb_monitor_s {
	enum monitor_cmd_type {
#define _X(menum, cmd, type, fn) menum,
		MONITOR_CMDS_X
#undef _X
	} type;
	const char *name;     ///< Name of monitor
	const char *argument; ///< Given argument to command / oid / op
	/// If the monitor is a vector response, how to name each sub-monitor
	const char *name_split_suffix;
	/** If the monitor is a vector response, each one will have a "instance"
	  key with value instance-%d */
	const char *instance_prefix;
	bool send;	    ///< Send the monitor to output or not
	bool integer;	 ///< Response must be an integer
	const char *splittok; ///< How to split response
	const char *splitop;  ///< Do a final operation with tokens
	const char *cmd_arg;  ///< Argument given to command
	json_object *enrichment;
};

static const char *rb_monitor_type(const rb_monitor_t *monitor) {
	assert(monitor);

	switch (monitor->type) {
#define _X(menum, cmd, type, fn)                                               \
	case menum:                                                            \
		return type;

		MONITOR_CMDS_X
#undef _X
	default:
		return NULL;
	};
}

const char *rb_monitor_name(const rb_monitor_t *monitor) {
	return monitor->name;
}

const json_object *rb_monitor_enrichment(const rb_monitor_t *monitor) {
	return monitor->enrichment;
}

const char *rb_monitor_instance_prefix(const rb_monitor_t *monitor) {
	return monitor->instance_prefix;
}

const char *rb_monitor_name_split_suffix(const rb_monitor_t *monitor) {
	return monitor->name_split_suffix;
}

bool rb_monitor_is_integer(const rb_monitor_t *monitor) {
	return monitor->integer;
}

bool rb_monitor_send(const rb_monitor_t *monitor) {
	return monitor->send;
}

const char *rb_monitor_get_cmd_data(const rb_monitor_t *monitor) {
	return monitor->argument;
}

void rb_monitor_get_op_variables(const rb_monitor_t *monitor,
				 char ***vars,
				 size_t *vars_size) {
	void *evaluator = NULL;
	(*vars) = NULL;
	*vars_size = 0;

	struct {
		char **vars;
		int count;
	} all_vars;

	if (monitor->type != RB_MONITOR_T__OP) {
		goto no_deps;
	}

	evaluator = evaluator_create((char *)monitor->cmd_arg);
	if (NULL == evaluator) {
		rdlog(LOG_ERR,
		      "Couldn't create an evaluator from %s",
		      monitor->cmd_arg);
		goto no_deps;
	}

	evaluator_get_variables(evaluator, &all_vars.vars, &all_vars.count);
	(*vars) = malloc((size_t)all_vars.count * sizeof((*vars)[0]));
	if (*vars == NULL) {
		rdlog(LOG_CRIT,
		      "Couldn't allocate memory for %d vars",
		      all_vars.count);
		goto no_deps;
	}
	for (int i = 0; i < all_vars.count; ++i) {
		(*vars)[i] = strdup(all_vars.vars[i]);
		if (NULL == (*vars)[i]) {
			rdlog(LOG_ERR,
			      "Couldn't strdup %s (OOM?)",
			      all_vars.vars[i]);
			for (int j = 0; j < i; ++j) {
				free((*vars)[j]);
				(*vars)[j] = NULL;
			}
			goto no_deps;
		}
	}
	*vars_size = (size_t)all_vars.count;
	evaluator_destroy(evaluator);
	return;

no_deps:
	if (*vars) {
		free(*vars);
	}
	*vars = NULL;
	*vars_size = 0;

	if (evaluator) {
		evaluator_destroy(evaluator);
	}
}

void rb_monitor_free_op_variables(char **vars, size_t vars_size) {
	for (size_t i = 0; i < vars_size; ++i) {
		free(vars[i]);
	}
	free(vars);
}

/** Free a const string.
  @todo remove this, is ugly
  */
static void free_const_str(const char *str) {
	char *aux;
	memcpy(&aux, &str, sizeof(aux));
	free(aux);
}

void rb_monitor_done(rb_monitor_t *monitor) {
	free_const_str(monitor->name);
	free_const_str(monitor->argument);
	free_const_str(monitor->name_split_suffix);
	free_const_str(monitor->instance_prefix);
	free_const_str(monitor->splittok);
	free_const_str(monitor->splitop);
	free_const_str(monitor->cmd_arg);
	if (monitor->enrichment) {
		json_object_put(monitor->enrichment);
	}
	free(monitor);
}

/** Get monitor command
  @param monitor Monitor to save command
  @param json_monitor JSON monitor to extract command
  */
static const char *
extract_monitor_cmd(enum monitor_cmd_type *type,
		    /* @todo const */ struct json_object *json_monitor) {
	static const char *cmd_keys[] = {
#define _X(menum, cmd, type, fn) cmd,
			MONITOR_CMDS_X
#undef _X
	};

	for (size_t i = 0; i < RD_ARRAYSIZE(cmd_keys); ++i) {
		struct json_object *json_cmd_arg = NULL;
		const bool get_rc = json_object_object_get_ex(
				json_monitor, cmd_keys[i], &json_cmd_arg);
		if (get_rc) {
			*type = i;
			return json_object_get_string(json_cmd_arg);
		}
	}

	return NULL;
}

/** Checks if a split operation is valid
  @param split_op requested split op
  @return true if valid, valse in other case
  */
static bool valid_split_op(const char *split_op) {
	const char *ops[] = {"sum", "mean"};
	for (size_t i = 0; i < RD_ARRAYSIZE(ops); ++i) {
		if (0 == strcmp(ops[i], split_op)) {
			return true;
		}
	}
	return false;
}

/** Parse a JSON monitor
  @param type Type of monitor (oid, system, op...)
  @param cmd_arg Argument of monitor (desired oid, system command, operation...)
  @return New monitor
  */
static rb_monitor_t *parse_rb_monitor0(enum monitor_cmd_type type,
				       const char *cmd_arg,
				       json_object *json_monitor,
				       json_object *sensor_enrichment) {
	assert(cmd_arg);
	assert(json_monitor);
	assert(sensor_enrichment);

	char *aux_name = PARSE_CJSON_CHILD_DUP_STR(json_monitor, "name", NULL);
	if (NULL == aux_name) {
		rdlog(LOG_ERR, "Monitor with no name");
		return NULL;
	}

	char *aux_split_op = PARSE_CJSON_CHILD_DUP_STR(
			json_monitor, "split_op", NULL);
	char *unit = PARSE_CJSON_CHILD_DUP_STR(json_monitor, "unit", NULL);
	char *group_name = PARSE_CJSON_CHILD_DUP_STR(
			json_monitor, "group_name", NULL);

	if (aux_split_op && !valid_split_op(aux_split_op)) {
		rdlog(LOG_WARNING,
		      "Invalid split op %s of monitor %s",
		      aux_split_op,
		      aux_name);
		free(aux_split_op);
		aux_split_op = NULL;
	}

	/// tmp monitor to locate all string parameters
	rb_monitor_t *ret = calloc(1, sizeof(*ret));
	if (NULL == ret) {
		rdlog(LOG_ERR, "Can't alloc sensor monitor (out of memory?)");
		free(aux_name);
		free(aux_split_op);
		free(unit);
		return NULL;
	}

	ret->splittok = PARSE_CJSON_CHILD_DUP_STR(json_monitor, "split", NULL);
	ret->splitop = aux_split_op;
	ret->name = aux_name;
	ret->name_split_suffix = PARSE_CJSON_CHILD_DUP_STR(
			json_monitor, "name_split_suffix", NULL);
	ret->instance_prefix = PARSE_CJSON_CHILD_DUP_STR(
			json_monitor, "instance_prefix", NULL);
	ret->send = PARSE_CJSON_CHILD_INT64(json_monitor, "send", 1);
	ret->integer = PARSE_CJSON_CHILD_INT64(json_monitor, "integer", 0);
	ret->type = type;
	ret->cmd_arg = strdup(cmd_arg);

	ret->enrichment = json_object_object_copy(sensor_enrichment);
	if (NULL == ret->enrichment) {
		rdlog(LOG_CRIT, "Couldn't allocate monitor enrichment (OOM?)");
		rb_monitor_done(ret);
		ret = NULL;
		goto err;
	}

#define RB_MONITOR_ENRICHMENT_STR(mkey, mval)                                  \
	{                                                                      \
		.key = (mval) ? (mkey) : NULL,                                 \
		.val = (mval) ? json_object_new_string(mval) : NULL,           \
	}

	// clang-format off
	const struct {
		const char *key;
		json_object *val;
	} enrichment_add[] = {
		{
			.key = "type",
			.val = json_object_new_string(rb_monitor_type(ret)),
		},

		RB_MONITOR_ENRICHMENT_STR("unit", unit),
		RB_MONITOR_ENRICHMENT_STR("group_name", group_name),
	};
	// clang-format on

	for (size_t i = 0; i < RD_ARRAYSIZE(enrichment_add); ++i) {
		/// @todo check additions
		if (enrichment_add[i].key) {
			json_object_object_add(ret->enrichment,
					       enrichment_add[i].key,
					       enrichment_add[i].val);
		}
	}

	if (NULL == ret->cmd_arg) {
		rdlog(LOG_CRIT, "Couldn't allocate cmd_arg (OOM?)");
		rb_monitor_done(ret);
		ret = NULL;
	}

err:
	free(group_name);
	free(unit);
	return ret;
}

rb_monitor_t *
parse_rb_monitor(json_object *json_monitor,
		 /* @todo const */ json_object *sensor_enrichment) {
	enum monitor_cmd_type cmd_type;
	const char *cmd_arg = extract_monitor_cmd(&cmd_type, json_monitor);
	if (NULL == cmd_arg) {
		rdlog(LOG_ERR, "Couldn't extract monitor command");
		return NULL;
	}

	rb_monitor_t *ret = parse_rb_monitor0(
			cmd_type, cmd_arg, json_monitor, sensor_enrichment);

	return ret;

	return NULL;
}

rb_monitor_t *
create_snmp_trap_rb_monitor(const char *name, json_object *enrichment) {
	char *monitor_name = strdup(name);
	if (alloc_unlikely(NULL == monitor_name)) {
		rdlog(LOG_ERR, "Couldn't allocate monitor name");
		return NULL;
	}

	rb_monitor_t *ret = calloc(1, sizeof(*ret));
	if (alloc_unlikely(NULL == ret)) {
		rdlog(LOG_ERR, "Couldn't allocate rb_monitor");
		free(monitor_name);
		return NULL;
	}

	ret->type = RB_MONITOR_T__OID;
	ret->name = monitor_name;
	ret->send = true;
	ret->enrichment = enrichment;
	return ret;
}

/** Context of sensor monitors processing */
struct process_sensor_monitor_ctx {
	struct monitor_snmp_session *snmp_sessp; ///< Base SNMP session
};

struct process_sensor_monitor_ctx *
new_process_sensor_monitor_ctx(struct monitor_snmp_session *snmp_sessp) {
	struct process_sensor_monitor_ctx *ret = calloc(1, sizeof(*ret));
	if (NULL == ret) {
		rdlog(LOG_ERR, "Couldn't allocate process sensor monitors ctx");
	} else {
		ret->snmp_sessp = snmp_sessp;
	}

	return ret;
}

void destroy_process_sensor_monitor_ctx(
		struct process_sensor_monitor_ctx *ctx) {
	free(ctx);
}

/** Base function to obtain an external value, and to manage it as a vector or
  as an integer
  @param monitor Monitor to process
  @param get_value_cb Callback to get value
  @param get_value_cb_ctx Context send to get_value_cb
  @return Monitor values array
  */
static struct monitor_value *
rb_monitor_get_external_value(const rb_monitor_t *monitor,
			      struct monitor_value *(*get_value_cb)(
					      const char *arg, void *ctx),
			      void *get_value_cb_ctx) {
	struct monitor_value *ret =
			get_value_cb(monitor->cmd_arg, get_value_cb_ctx);

	if (unlikely(!ret)) {
		return NULL;
	}

	if (monitor->splittok) {
		const bool array_rc = new_monitor_value_array_from_string(
				ret, monitor->splittok, monitor->splitop);
		if (unlikely(!array_rc)) {
			rb_monitor_value_done(ret);
			return NULL;
		}
	}

	return ret;
}

/** Convenience function to obtain system values */
static struct monitor_value *rb_monitor_get_system_external_value(
		const rb_monitor_t *monitor,
		struct process_sensor_monitor_ctx *process_ctx,
		rb_monitor_value_array_t *ops_vars) {
	(void)process_ctx;
	(void)ops_vars;
	return rb_monitor_get_external_value(
			monitor, system_solve_response, NULL);
}

/// Wrapper function to transform void -> snmp_session
static monitor_value *
snmp_solve_response0(const char *oid_string, void *snmp_session) {
	return snmp_query_response(oid_string, snmp_session);
}

/** Convenience function to obtain SNMP values */
static struct monitor_value *rb_monitor_get_snmp_external_value(
		const rb_monitor_t *monitor,
		struct process_sensor_monitor_ctx *process_ctx,
		rb_monitor_value_array_t *op_vars) {
	(void)op_vars;
	return rb_monitor_get_external_value(
			monitor, snmp_solve_response0, process_ctx->snmp_sessp);
}

/** Create a libmatheval vars using op_vars */
static struct libmatheval_vars *
op_libmatheval_vars(rb_monitor_value_array_t *op_vars, char **names) {
	struct libmatheval_vars *libmatheval_vars =
			new_libmatheval_vars(op_vars->count);
	size_t expected_v_elms = 0;
	if (NULL == libmatheval_vars) {
		/// @todo error treatment
		return NULL;
	}

	for (size_t i = 0; i < op_vars->count; ++i) {
		assert(rb_monitor_value_array_at(op_vars, 0)->type ==
		       rb_monitor_value_array_at(op_vars, i)->type);

		struct monitor_value *mv =
				rb_monitor_value_array_at(op_vars, i);

		if (mv) {
			libmatheval_vars->names[i] = names[i];

			if (0 == libmatheval_vars->count) {
				if (MONITOR_VALUE_T__ARRAY == mv->type) {
					expected_v_elms =
							mv->array.children_count;
				}
			} else if (mv->type == MONITOR_VALUE_T__ARRAY &&
				   mv->array.children_count !=
						   expected_v_elms) {
				rdlog(LOG_ERR,
				      "trying to operate on vectors of "
				      "different size:"
				      "[(previous size):%zu] != [%s:%zu]",
				      expected_v_elms,
				      names[i],
				      mv->array.children_count);
				goto err;
			}

			libmatheval_vars->count++;
		}
	}

	return libmatheval_vars;
err:
	delete_libmatheval_vars(libmatheval_vars);
	return NULL;
}

/** Do a monitor operation
  @param f evaluator
  @param libmatheval_vars prepared libmathevals with names and values
  @param monitor Monitor operation belongs
  @return New monitor value
  */
static struct monitor_value *
rb_monitor_op_value0(void *f,
		     struct libmatheval_vars *libmatheval_vars,
		     const rb_monitor_t *monitor) {

	const char *operation = monitor->cmd_arg;
	const double number = evaluator_evaluate(f,
						 libmatheval_vars->count,
						 libmatheval_vars->names,
						 libmatheval_vars->values);

	rdlog(LOG_DEBUG,
	      "Result of operation [%s]: %lf",
	      monitor->cmd_arg,
	      number);

	if (!isnormal(number)) {
		rdlog(LOG_ERR,
		      "OP %s return a bad value: %lf. Skipping.",
		      operation,
		      number);
		return NULL;
	}

	return new_monitor_value(number);
}

/** Do a monitor value operation, with no array involved
  @param f Evaluator
  @param op_vars Operations variables with names
  @param monitor Montior this operation belongs
  @return new monitor value with operation result
  */
static struct monitor_value *
rb_monitor_op_value(void *f,
		    rb_monitor_value_array_t *op_vars,
		    struct libmatheval_vars *libmatheval_vars,
		    const rb_monitor_t *monitor) {

	/* Foreach variable in operation, value */
	for (size_t v = 0; v < op_vars->count; ++v) {
		const struct monitor_value *mv_v =
				rb_monitor_value_array_at(op_vars, v);
		assert(mv_v);
		assert(MONITOR_VALUE_T__ARRAY != mv_v->type);
		libmatheval_vars->values[v] = monitor_value_double(mv_v);
	}

	return rb_monitor_op_value0(f, libmatheval_vars, monitor);
}

/** Gets an operation result of vector position i
  @param f evaluator
  @param libmatheval_vars Libmatheval prepared variables
  @param v_pos Vector position we want to evaluate
  @param monitor Monitor this operation belongs
  @return Montior value with vector index i of the result
  @todo merge with rb_monitor_op_value
  */
static struct monitor_value *
rb_monitor_op_vector_i(void *f,
		       rb_monitor_value_array_t *op_vars,
		       struct libmatheval_vars *libmatheval_vars,
		       size_t v_pos,
		       const rb_monitor_t *monitor) {
	/* Foreach variable in operation, use element i of vector */
	for (size_t v = 0; v < op_vars->count; ++v) {
		const struct monitor_value *mv_v =
				rb_monitor_value_array_at(op_vars, v);
		assert(mv_v);
		assert(MONITOR_VALUE_T__ARRAY == mv_v->type);

		const struct monitor_value *mv_v_i =
				mv_v->array.children[v_pos];
		if (NULL == mv_v_i) {
			// We don't have this value, so we can't do operation
			return NULL;
		}

		assert(MONITOR_VALUE_T__ARRAY != mv_v_i->type);

		libmatheval_vars->values[v] = monitor_value_double(mv_v_i);
	}

	return rb_monitor_op_value0(f, libmatheval_vars, monitor);
}

/** Makes a vector operation
  @param f libmatheval evaluator
  @param op_vars Monitor values of operation variables
  @param libmatheval_vars Libmatheval variables template
  @param monitor Monitor this operation belongs
  @todo op_vars should be const
  */
static struct monitor_value *
rb_monitor_op_vector(void *f,
		     rb_monitor_value_array_t *op_vars,
		     struct libmatheval_vars *libmatheval_vars,
		     const rb_monitor_t *monitor) {
	double sum = 0;
	size_t count = 0;
	const struct monitor_value *mv_0 =
			rb_monitor_value_array_at(op_vars, 0);
	struct monitor_value *split_op = NULL;
	struct monitor_value **children =
			calloc(mv_0->array.children_count, sizeof(children[0]));
	if (NULL == children) {
		/* @todo Error treatment */
		rdlog(LOG_ERR,
		      "Couldn't create monitor value %s"
		      " children (out of memory?)",
		      monitor->name);
		return NULL;
	}

	// Foreach member of vector
	for (size_t i = 0; i < mv_0->array.children_count; ++i) {
		children[i] = rb_monitor_op_vector_i(
				f, op_vars, libmatheval_vars, i, monitor);

		if (NULL != children[i]) {
			sum += monitor_value_double(children[i]);
			count++;
		}
	} /* foreach member of vector */

	if (monitor->splitop && count > 0) {
		const double split_op_value =
				0 == strcmp(monitor->splitop, "sum")
						? sum
						: sum / count;

		split_op = new_monitor_value(split_op_value);
	}

	return new_monitor_value_array(
			mv_0->array.children_count, children, split_op);
}

/** Process an operation monitor
  @param monitor Monitor this operation belongs
  @param sensor Sensor this monitor belongs
  @param process_ctx Monitor process context
  @return New monitor value with operation result
  */
static struct monitor_value *
rb_monitor_get_op_result(const rb_monitor_t *monitor,
			 struct process_sensor_monitor_ctx *process_ctx,
			 rb_monitor_value_array_t *op_vars) {
	(void)process_ctx;
	struct monitor_value *ret = NULL;
	const char *operation = monitor->cmd_arg;

	struct {
		char **vars;
		int vars_len;
	} f_vars;

	/// @todo error treatment in this cases
	if (NULL == op_vars) {
		return NULL;
	}

	if (0 == op_vars->count) {
		return NULL;
	}

	void *const f = evaluator_create((char *)operation);
	if (NULL == f) {
		rdlog(LOG_ERR,
		      "Couldn't create evaluator (invalid op [%s]?",
		      operation);
		return NULL;
	}

	evaluator_get_variables(f, &f_vars.vars, &f_vars.vars_len);

	struct libmatheval_vars *libmatheval_vars =
			op_libmatheval_vars(op_vars, f_vars.vars);
	if (NULL == libmatheval_vars) {
		goto libmatheval_error;
	}

	const struct monitor_value *mv_0 =
			rb_monitor_value_array_at(op_vars, 0);
	if (mv_0) {
		ret = ((MONITOR_VALUE_T__ARRAY == mv_0->type)
				       ? rb_monitor_op_vector
				       : rb_monitor_op_value)(
				f, op_vars, libmatheval_vars, monitor);
	}

	delete_libmatheval_vars(libmatheval_vars);
libmatheval_error:
	evaluator_destroy(f);
	return ret;
}

struct monitor_value *
process_sensor_monitor(struct process_sensor_monitor_ctx *process_ctx,
		       const rb_monitor_t *monitor,
		       rb_monitor_value_array_t *op_vars) {
	switch (monitor->type) {
#define _X(menum, cmd, type, fn)                                               \
	case menum:                                                            \
		return fn(monitor, process_ctx, op_vars);                      \
		break;

		MONITOR_CMDS_X
#undef _X

	default:
		rdlog(LOG_CRIT, "Unknown monitor type: %u", monitor->type);
		return NULL;
	}; /* Switch monitor type */
}
