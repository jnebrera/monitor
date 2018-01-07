#!/usr/bin/env sh

readonly OUT_FILE=config.json

# Assign default value if not value
function zz_var {
	eval "local readonly currval=\"\$$1\""
	if [ -z "${currval}" ]; then
		value="$(printf "%s" "$2" | sed 's%"%\\"%g')"
		eval "export $1=\"$value\""
	fi
}

#
# ZZ variables
#

zz_var MONITOR_NTHREADS 3
zz_var REQUESTS_TIMEOUT 25
zz_var KAFKA_BROKERS kafka
zz_var KAFKA_TOPIC 'monitor'
zz_var SENSORS_ARRAY ''
zz_var TRAPS_PORT ''

if [ ! -z "$SENSORS_ARRAY" ]; then
	export sensors="\"sensors\":[${SENSORS_ARRAY}]"
fi

if [ ! -z "$TRAPS_PORT" ]; then
	snmp_server="0.0.0.0:$TRAPS_PORT"
	export snmp_traps_config="\"snmp_traps\":{\"server_name\":\"$snmp_server\"},"
fi

#
# All RDKAFKA_ vars will be passed to librdkafka as-is
#

# Override librdkafka defaults
zz_var RDKAFKA_SOCKET_KEEPALIVE_ENABLE true
zz_var RDKAFKA_MESSAGE_SEND_MAX_RETRIES 0
zz_var RDKAFKA_API_VERSION_REQUEST true

export kafka_opts=""

# Read all librdkafka envs, chop first RDKAFKA, and change '_' for '.'
while IFS='=' read rdkafka_key rdkafka_val; do
	kafka_opts="$kafka_opts\"$rdkafka_key\":\"$rdkafka_val\","
done <<EOF
$(env | grep '^RDKAFKA_' | tr 'A-Z_' 'a-z.')
EOF

# Delete last comma & pretty printing
kafka_opts=$(printf '%s' "${kafka_opts%,}" | sed 's%,%,\n  %g')

envsubst < ${OUT_FILE}.env > ${OUT_FILE}

exec ./rb_monitor -c ${OUT_FILE}
