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

#include "rb_sensor.h"

#include "rb_json.h"

#include "rb_sensor_monitor_array.h"

#include "utils.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>
#include <librd/rdlog.h>

static const char SENSOR_NAME_ENRICHMENT_KEY[] = "sensor_name";
static const char SENSOR_ID_ENRICHMENT_KEY[] = "sensor_id";

/// Sensor to monitor
struct rb_sensor_s {
#ifndef NDEBUG
#define RB_SENSOR_MAGIC 0xB30A1CB30A1CL
	uint64_t magic;
#endif

	struct monitor_snmp_session snmp_sess; ///< SNMP session
	rb_monitors_array_t *monitors;	 ///< Monitors to ask for
	ssize_t **op_vars; ///< Operation variables that needs each monitor
	json_object *enrichment; ///< Enrichment to use in monitors
	int refcnt;		 ///< Reference counting
};

#ifdef RB_SENSOR_MAGIC
void assert_rb_sensor(rb_sensor_t *sensor) {
	assert(RB_SENSOR_MAGIC == sensor->magic);
}
#endif

/** We assume that sensor name is only requested in config errors, so we only
  save it in enrichment json
 * @param sensor Sensor to obtain string
 * @return Sensor name or "(some_sensor)" string if not defined
 */
const char *rb_sensor_name(const rb_sensor_t *sensor) {
	assert(sensor);

	json_object *jsensor_name = NULL;
	const bool get_rc =
			json_object_object_get_ex(sensor->enrichment,
						  SENSOR_NAME_ENRICHMENT_KEY,
						  &jsensor_name);

	assert(get_rc);
	(void)get_rc;

	return json_object_get_string(jsensor_name);
}

/** Sensor enrichment information */
struct sensor_enrichment {
	const char *sensor_name; ///< Sensor name
	int64_t sensor_id;       ///< Sensor id
};

monitor_snmp_session *rb_sensor_snmp_session(rb_sensor_t *sensor) {
	return &sensor->snmp_sess;
}

/**
 * Create sensor enrichment
 * @param  data              Data to enrich with
 * @param  sensor_enrichment Struct to hold data
 * @return                   Bool if success, false in other way
 */
static bool sensor_create_enrichment(const struct sensor_enrichment *data,
				     json_object *sensor_enrichment) {
	char errbuf[BUFSIZ];

	assert(data);
	assert(data->sensor_name);
	assert(sensor_enrichment);

	const bool name_rc = ADD_JSON_STRING(sensor_enrichment,
					     SENSOR_NAME_ENRICHMENT_KEY,
					     data->sensor_name,
					     errbuf,
					     sizeof(errbuf));
	if (!name_rc) {
		rdlog(LOG_ERR,
		      "Couldn't add sensor name to enrichment: %s",
		      errbuf);
		return false;
	}

	if (data->sensor_id > 0) {
		const bool id_rc = ADD_JSON_INT64(sensor_enrichment,
						  SENSOR_ID_ENRICHMENT_KEY,
						  data->sensor_id,
						  errbuf,
						  sizeof(errbuf));
		if (!id_rc) {
			rdlog(LOG_ERR,
			      "Couldn't add sensor id to enrichment: %s",
			      errbuf);
			return false;
		}
	}

	return true;
}

/** Fill sensor information
  @param sensor Sensor to store information
  @param sensor_info JSON describing sensor
  */
static bool
sensor_common_attrs_parse_json(rb_sensor_t *sensor,
			       /* const */ json_object *sensor_info) {
	struct json_object *sensor_monitors = NULL;
	// clang-format off
	const struct sensor_enrichment sensor_enrichment = {
		.sensor_id = PARSE_CJSON_CHILD_INT64(
			sensor_info, SENSOR_ID_ENRICHMENT_KEY, 0),
		.sensor_name = PARSE_CJSON_CHILD_STR(
			sensor_info, SENSOR_NAME_ENRICHMENT_KEY, NULL)
	};
	// clang-format on

	if (NULL == sensor_enrichment.sensor_name) {
		rdlog(LOG_ERR, "Sensor with no name, couldn't parse");
		goto err;
	}

	json_object_object_get_ex(sensor_info, "monitors", &sensor_monitors);
	if (NULL == sensor_monitors) {
		rdlog(LOG_ERR,
		      "Could not obtain JSON sensors monitors. "
		      "Skipping");
		goto err;
	}

	json_object_object_get_ex(
			sensor_info, "enrichment", &sensor->enrichment);
	if (NULL == sensor->enrichment) {
		sensor->enrichment = json_object_new_object();
		if (NULL == sensor->enrichment) {
			rdlog(LOG_CRIT,
			      "Couldn't allocate sensor %s enrichment",
			      sensor_enrichment.sensor_name);
			goto err;
		}
	}

	const bool create_enrichment_rc = sensor_create_enrichment(
			&sensor_enrichment, sensor->enrichment);

	sensor->monitors =
			parse_rb_monitors(sensor_monitors, sensor->enrichment);
	if (!create_enrichment_rc) {
		goto err;
	}

	if (NULL != sensor->monitors) {
		sensor->op_vars = get_monitors_dependencies(sensor->monitors);
	} else {
		goto err;
	}

	return true;

err:
	return false;
}

/** Extract sensor common properties to all monitors
  @param sensor_data Return value
  @param sensor_info Original JSON to extract information
  @todo recorver sensor_info const (in modern cjson libraries)
  */
static bool sensor_common_attrs(rb_sensor_t *sensor,
				/* const */ json_object *sensor_info) {
	return sensor_common_attrs_parse_json(sensor, sensor_info);
}

/** Sets sensor defaults
  @param worker_info Worker info that contains defaults
  @param sensor Sensor to store defaults
  */
static void sensor_set_defaults(rb_sensor_t *sensor) {
#ifdef RB_SENSOR_MAGIC
	sensor->magic = RB_SENSOR_MAGIC;
#endif
	sensor->refcnt = 1;
}

static bool sensor_parse_snmp(rb_sensor_t *sensor, json_object *sensor_info) {
	const char *community =
			PARSE_CJSON_CHILD_STR(sensor_info, "community", NULL);
	if (!community) {
		// No SNMP in this sensor
		return true;
	}

	/// @TODO do this need to live after snmp_sess creation?
	netsnmp_session sess_config;
	snmp_sess_init(&sess_config);

#define UPDATE_MEMBER(sess, member, parse_cb, sensor_json, sensor_option_name) \
	sess.member = parse_cb(sensor_json, sensor_option_name, (sess).member)

	const char *snmp_version = PARSE_CJSON_CHILD_STR(
			sensor_info, "snmp_version", NULL);
	sess_config.version =
			snmp_version ? net_snmp_version(snmp_version,
							rb_sensor_name(sensor))
				     : SNMP_VERSION_2c;

	sess_config.community_len = strlen(community);
	/// copying this way because of lack of const qualifier
	memcpy(&sess_config.community,
	       &community,
	       sizeof(sess_config.community));

	UPDATE_MEMBER(sess_config,
		      retries,
		      PARSE_CJSON_CHILD_INT64,
		      sensor_info,
		      "retries");
	UPDATE_MEMBER(sess_config,
		      timeout,
		      PARSE_CJSON_CHILD_INT64,
		      sensor_info,
		      "timeout");
	/// @TODO do we need to duplicate string?
	UPDATE_MEMBER(sess_config,
		      peername,
		      PARSE_CJSON_CHILD_DUP_STR,
		      sensor_info,
		      "sensor_ip");

	if (NULL == sess_config.peername) {
		sess_config.peername = "localhost:161";
	}

	return new_snmp_session(&sensor->snmp_sess, &sess_config);
}

/// @TODO make sensor_info const
rb_sensor_t *parse_rb_sensor(/* const */ json_object *sensor_info) {
	rb_sensor_t *ret = calloc(1, sizeof(*ret));

	if (alloc_unlikely(!ret)) {
		rdlog(LOG_ERR, "Couldn't allocate sensor (OOM?)");
		return NULL;
	}

	sensor_set_defaults(ret);
	const bool common_attrs_ok = sensor_common_attrs(ret, sensor_info);
	if (!common_attrs_ok) {
		goto sensor_common_attrs_err;
	}

	const bool snmp_parser_ok = sensor_parse_snmp(ret, sensor_info);
	if (unlikely(!snmp_parser_ok)) {
		goto snmp_parse_err;
	}

	return ret;

snmp_parse_err:
sensor_common_attrs_err:
	rb_sensor_put(ret);
	return NULL;
}

/** Process a sensor
  @param sensor Sensor
  @param ret Messages returned
  @return true if OK, false in other case
  */
bool process_rb_sensor(rb_sensor_t *sensor, rb_message_list *ret) {
	return process_monitors_array(
			sensor, sensor->monitors, sensor->op_vars, ret);
}

/** Free allocated memory for sensor
  @param sensor Sensor to free
  */
static void sensor_done(rb_sensor_t *sensor) {
	destroy_snmp_session(&sensor->snmp_sess);
	if (sensor->op_vars) {
		free_monitors_dependencies(sensor->op_vars,
					   sensor->monitors->count);
	}
	if (sensor->monitors) {
		rb_monitors_array_done(sensor->monitors);
	}
	if (sensor->enrichment) {
		json_object_put(sensor->enrichment);
	}
	free(sensor);
}

void rb_sensor_get(rb_sensor_t *sensor) {
	ATOMIC_OP(add, fetch, &sensor->refcnt, 1);
}

void rb_sensor_put(rb_sensor_t *sensor) {
	if (0 == ATOMIC_OP(sub, fetch, &sensor->refcnt, 1)) {
		sensor_done(sensor);
	}
}
