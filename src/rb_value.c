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

#include "rb_value.h"

#include "rb_sensor.h"
#include "rb_sensor_monitor.h"

#include <json-c/printbuf.h>
#include <librd/rdlog.h>
#include <librd/rdmem.h>

#include <ctype.h>

double monitor_value_double(const struct monitor_value *mv) {
	switch (mv->type) {
	case MONITOR_VALUE_T__DOUBLE:
		return mv->value.value_d;
	case MONITOR_VALUE_T__ARRAY:
	case MONITOR_VALUE_T__BAD:
	case MONITOR_VALUE_T__STRING:
	default:
		rdlog(LOG_ERR,
		      "Trying to extract double of a non-number monitor");
		return 0;
	};
}

#ifdef MONITOR_VALUE_MAGIC
#define new_monitor_value_magic_member(mv) (mv)->magic = MONITOR_VALUE_MAGIC
#else
#define new_monitor_value_magic_member(mv)
#endif

// clang-format off
#define new_monitor_value0(t_type, t_union_member, t_value) ({                 \
	monitor_value *new_monitor_value0 =                                    \
				calloc(1, sizeof *new_monitor_value0);         \
	if (alloc_likely(NULL != new_monitor_value0)) {                        \
		new_monitor_value0->type = t_type;                             \
		new_monitor_value0->value.t_union_member = t_value;            \
		new_monitor_value_magic_member(new_monitor_value0);            \
	}                                                                      \
	new_monitor_value0;                                                    \
})
// clang-format on

monitor_value *new_monitor_value_int(long i) {
	return new_monitor_value_double(i);
}

monitor_value *new_monitor_value_double(double dbl) {
	return new_monitor_value0(MONITOR_VALUE_T__DOUBLE, value_d, dbl);
}

monitor_value *new_monitor_value_strn(char *str, size_t str_len) {
	// Check if we can transform string to a double
	char *endptr;
	const double val_d = strtod(str, &endptr);
	if (endptr == &str[str_len]) {
		// All string was a double!
		free(str);
		return new_monitor_value(val_d);
	}

	// Ok, it's a simple string
	monitor_value *ret = malloc(sizeof(*ret));
	if (alloc_unlikely(!ret)) {
		return NULL;
	}

#ifdef MONITOR_VALUE_MAGIC
	ret->magic = MONITOR_VALUE_MAGIC;
#endif

	ret->type = MONITOR_VALUE_T__STRING;
	ret->value.value_s.buf = str;
	ret->value.value_s.size = str_len;
	return ret;
}

/// Calls GNU GETLINE and chop final blanks
static ssize_t getline_chop(char **lineptr, size_t *n, FILE *stream) {
	ssize_t ret = getline(lineptr, n, stream);
	if (ret <= 0) {
		return ret;
	}

	while (ret > 1 && isspace((*lineptr)[ret - 1])) {
		(*lineptr)[ret] = '\0';
		ret--;
	}

	return ret;
}

monitor_value *new_monitor_value_fstream(FILE *stream) {
	struct {
		char *buf;
		size_t buf_size;
	} sread = {0};

	const ssize_t bytes_read =
			getline_chop(&sread.buf, &sread.buf_size, stream);
	if (unlikely(bytes_read < 0)) {
		rdlog(LOG_ERR,
		      "Cannot get buffer information: %s",
		      gnu_strerror_r(errno));
		free(sread.buf);
		return NULL;
	}

	return new_monitor_value_strn(sread.buf, (size_t)bytes_read);
}

/** Count the number of elements of a vector response
  @param haystack String of values
  @param splittok Token that sepparate values
  @return Number of elements
  */
static size_t vector_elements(const char *haystack, const char *splittok) {
	size_t ret = 1;
	for (ret = 1; (haystack = strstr(haystack, splittok));
	     haystack += strlen(splittok), ++ret) {
		;
	}
	return ret;
}

/** strtod conversion plus set errno=EINVAL if no conversion is possible
  @param str input string
  @return double
  */
static double toDouble(const char *str) {
	char *endPtr;
	errno = 0;
	double d = strtod(str, &endPtr);
	return d;
}

/** Extract value of a vector
  @param vector_values String to extract values from
  @param splittok Split token
  @return Next token to iterate
  */
static bool extract_vector_value(const char *vector_values,
				 const char *splittok,
				 double *value) {

	const char *end_token = strstr(vector_values, splittok);
	if (NULL == end_token) {
		end_token = vector_values + strlen(vector_values);
	}

	*value = toDouble(vector_values);
	if (errno != 0) {
		const char *perrbuf = gnu_strerror_r(errno);
		rdlog(LOG_WARNING,
		      "Invalid double: %.*s (%s). Not counting.",
		      (int)(end_token - vector_values),
		      vector_values,
		      perrbuf);
		return false;
	}

	return true;
}

monitor_value *new_monitor_value_array(size_t children_count,
				       monitor_value **children,
				       monitor_value *split_op_result) {
	monitor_value *ret = calloc(1, sizeof(*ret));
	if (alloc_unlikely(!ret)) {
		return NULL;
	}
#ifdef MONITOR_VALUE_MAGIC
	ret->magic = MONITOR_VALUE_MAGIC;
#endif

	ret->type = MONITOR_VALUE_T__ARRAY;
	ret->array.children_count = children_count;
	ret->array.split_op_result = split_op_result;
	ret->array.children = children;

	return ret;
}

bool new_monitor_value_array_from_string(monitor_value *mv,
					 const char *split_tok,
					 const char *split_op) {
	struct {
		char *buf;
		size_t size;
	} src_str = {
			.buf = mv->value.value_s.buf,
			.size = mv->value.value_s.size,
	};

	if (unlikely(MONITOR_VALUE_T__STRING != mv->type)) {
		rdlog(LOG_ERR,
		      "Trying to process not-string result as a vector");
		return NULL;
	}

	memset(mv, 0, sizeof(*mv));
#ifdef MONITOR_VALUE_MAGIC
	mv->magic = MONITOR_VALUE_MAGIC;
#endif

	mv->type = MONITOR_VALUE_T__ARRAY;

	mv->array.children_count = vector_elements(src_str.buf, split_tok);
	const char *tok = NULL;

	mv->array.children = calloc(mv->array.children_count,
				    sizeof(mv->array.children[0]));
	if (alloc_unlikely(NULL == mv->array.children)) {
		rdlog(LOG_ERR,
		      "Couldn't allocate vector children (out of memory?)");
		return NULL;
	}

	size_t mean_count = 0, count = 0;
	double sum = 0;
	for (count = 0, tok = src_str.buf; tok;
	     tok = strstr(tok, split_tok), count++) {
		if (count > 0) {
			// Skip delimiter
			tok += strlen(split_tok);
		}

		double i_value = 0;

		if (0 == strcmp(tok, split_tok)) {
			// Empty token
			rdlog(LOG_DEBUG, "Not seeing value %zu", count);
			continue;
		}

		const bool get_value_rc =
				extract_vector_value(tok, split_tok, &i_value);

		if (false == get_value_rc) {
			continue;
		}

		mv->array.children[count] = new_monitor_value(i_value);
		if (alloc_unlikely(!mv->array.children[count])) {
			continue;
		}

		sum += i_value;
		mean_count++;
	}

	// Last token reached. Do we have an operation to do?
	if (NULL != split_op && mean_count > 0) {
		const double result = (0 == strcmp("sum", split_op))
						      ? sum
						      : sum / mean_count;

		mv->array.split_op_result = new_monitor_value(result);
	}

	free(src_str.buf);
	return true;
}

static void print_monitor_value_enrichment_str(struct printbuf *buf,
					       const char *key,
					       json_object *val) {
	const char *str = json_object_get_string(val);
	if (NULL == str) {
		rdlog(LOG_ERR,
		      "Cannot extract string value of enrichment key %s",
		      key);
	} else {
		sprintbuf(buf, ",\"%s\":\"%s\"", key, str);
	}
}

static void print_monitor_value_enrichment_int(struct printbuf *buf,
					       const char *key,
					       json_object *val) {
	errno = 0;
	int64_t integer = json_object_get_int64(val);
	if (errno != 0) {
		const char *errbuf = gnu_strerror_r(errno);
		rdlog(LOG_ERR,
		      "Cannot extract int value of enrichment key %s: %s",
		      key,
		      errbuf);
	} else {
		sprintbuf(buf, ",\"%s\":%ld", key, integer);
	}
}

/// @TODO we should print all with this function
static void
print_monitor_value_enrichment(struct printbuf *buf,
			       const json_object *const_enrichment) {
	json_object *enrichment = (json_object *)const_enrichment;

	for (struct json_object_iterator i = json_object_iter_begin(enrichment),
					 end = json_object_iter_end(enrichment);
	     !json_object_iter_equal(&i, &end);
	     json_object_iter_next(&i)) {
		const char *key = json_object_iter_peek_name(&i);
		json_object *val = json_object_iter_peek_value(&i);

		const json_type type = json_object_get_type(val);
		switch (type) {
		case json_type_string:
			print_monitor_value_enrichment_str(buf, key, val);
			break;

		case json_type_int:
			print_monitor_value_enrichment_int(buf, key, val);
			break;

		case json_type_null:
			sprintbuf(buf, ",\"%s\":null", key);
			break;

		case json_type_boolean: {
			const json_bool b = json_object_get_boolean(val);
			sprintbuf(buf,
				  ",\"%s\":%s",
				  key,
				  b == FALSE ? "false" : "true");
			break;
		}
		case json_type_double: {
			const double d = json_object_get_double(val);
			sprintbuf(buf, ",\"%s\":%lf", key, d);
			break;
		}
		case json_type_object:
		case json_type_array: {
			rdlog(LOG_ERR,
			      "Can't enrich with objects/array at this time");
			break;
		}
		default:
			rdlog(LOG_ERR,
			      "Don't know how to duplicate JSON type %d",
			      type);
			break;
		};
	}
}

#define NO_INSTANCE (-1)
static void print_monitor_value0(rb_message *message,
				 const struct monitor_value *t_monitor_value,
				 const rb_monitor_t *monitor,
				 int instance) {
	struct printbuf *buf = printbuf_new();
	if (alloc_unlikely((!buf))) {
		rdlog(LOG_ERR, "Couldn't allocate print buffer (OOM?)");
		return;
	}

	const char *monitor_instance_prefix =
			rb_monitor_instance_prefix(monitor);
	const char *monitor_name_split_suffix =
			rb_monitor_name_split_suffix(monitor);
	const struct json_object *monitor_enrichment =
			rb_monitor_enrichment(monitor);
	// @TODO use printbuf_memappend_fast instead!
	sprintbuf(buf, "{");
	sprintbuf(buf, "\"timestamp\":%tu", time(NULL));
	if (NO_INSTANCE != instance && monitor_name_split_suffix) {
		sprintbuf(buf,
			  ",\"monitor\":\"%s%s\"",
			  rb_monitor_name(monitor),
			  monitor_name_split_suffix);
	} else {
		sprintbuf(buf, ",\"monitor\":\"%s\"", rb_monitor_name(monitor));
	}

	if (NO_INSTANCE != instance && monitor_instance_prefix) {
		sprintbuf(buf,
			  ",\"instance\":\"%s%d\"",
			  monitor_instance_prefix,
			  instance);
	}

	sprintbuf(buf, ",\"value\":");
	switch (t_monitor_value->type) {
	case MONITOR_VALUE_T__DOUBLE:
		sprintbuf(buf, "\"%lf\"", t_monitor_value->value.value_d);
		break;
	case MONITOR_VALUE_T__STRING:
		sprintbuf(buf,
			  "\"%.*s\"",
			  t_monitor_value->value.value_s.size,
			  t_monitor_value->value.value_s.buf);
		break;
	case MONITOR_VALUE_T__BAD:
	case MONITOR_VALUE_T__ARRAY:
	default:
		break;
	};

	if (monitor_enrichment) {
		print_monitor_value_enrichment(buf, monitor_enrichment);
	}
	sprintbuf(buf, "}");

	message->payload = buf->buf;
	message->len = (size_t)buf->bpos;

	buf->buf = NULL;
	printbuf_free(buf);
}

rb_message_array_t *
print_monitor_value(const struct monitor_value *t_monitor_value,
		    const rb_monitor_t *monitor) {
	// clang-format off
	const size_t ret_size = (t_monitor_value->type == MONITOR_VALUE_T__ARRAY)
		? t_monitor_value->array.children_count +
		  (t_monitor_value->array.split_op_result ? 1 : 0)
		: 1;
	// clang-format on

	rb_message_array_t *ret = new_messages_array(ret_size);
	if (ret == NULL) {
		rdlog(LOG_ERR, "Couldn't allocate messages array");
		return NULL;
	}

	if (t_monitor_value->type == MONITOR_VALUE_T__ARRAY) {
		size_t i_msgs = 0;
		assert(t_monitor_value->type == MONITOR_VALUE_T__ARRAY);
		for (size_t i = 0; i < t_monitor_value->array.children_count;
		     ++i) {
			if (t_monitor_value->array.children[i]) {
				print_monitor_value0(
						&ret->msgs[i_msgs++],
						t_monitor_value->array
								.children[i],
						monitor,
						i);
			}
		}

		if (t_monitor_value->array.split_op_result) {
			rb_message *msg = &ret->msgs[i_msgs++];
			assert(NULL == msg->payload);
			print_monitor_value0(
					msg,
					t_monitor_value->array.split_op_result,
					monitor,
					NO_INSTANCE);
		}

		ret->count = i_msgs;
	} else {
		print_monitor_value0(&ret->msgs[0],
				     t_monitor_value,
				     monitor,
				     NO_INSTANCE);
	}

	return ret;
}

static size_t pos_array_length(const ssize_t *pos) {
	assert(pos);
	size_t i = 0;
	for (i = 0; - 1 != pos[i]; ++i) {
		;
	}
	return i;
}

rb_monitor_value_array_t *
rb_monitor_value_array_select(rb_monitor_value_array_t *array, ssize_t *pos) {
	if (NULL == pos || NULL == array) {
		return NULL;
	}

	const size_t ret_size = pos_array_length(pos);
	rb_monitor_value_array_t *ret = rb_monitor_value_array_new(ret_size);
	if (NULL == ret) {
		rdlog(LOG_ERR, "Couldn't allocate select return (OOM?)");
		return NULL;
	}

	assert(array);
	assert(pos);

	for (size_t i = 0; - 1 != pos[i]; ++i) {
		rb_monitor_value_array_add(ret, array->elms[pos[i]]);
	}

	return ret;
}

void rb_monitor_value_done(struct monitor_value *mv) {
	if (MONITOR_VALUE_T__ARRAY == mv->type) {
		for (size_t i = 0; i < mv->array.children_count; ++i) {
			if (mv->array.children[i]) {
				rb_monitor_value_done(mv->array.children[i]);
			}
		}
		if (mv->array.split_op_result) {
			rb_monitor_value_done(mv->array.split_op_result);
		}
		free(mv->array.children);
	}
	free(mv);
}
