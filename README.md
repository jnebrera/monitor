[![CircleCI](https://circleci.com/gh/wizzie-io/rb_monitor.svg?style=svg&circle-token=22d517d1196fe8208eedd8341cf4c06e3f6fbeab)](https://circleci.com/gh/wizzie-io/rb_monitor)

# rb_monitor

## Introduction

Monitor agent, that sends periodically any kind of stat via kafka or http.

You can monitor your system using periodical SNMP (or pure raw system commands) and send them via kafka (or HTTP POST) to a central event ingestion system. You
can also send SNMP traps and `monitor` will send them via kafka.

## Code Samples

### Usage of rb_monitor
To run rb_monitor, you have to prepare the config file to monitor the parameters you want, and run it using `rb_monitor -c <command-file>`

Config file are divided in two sections:

1. `conf` one is more generic, and allow to tune different parameters in kafka or the own rb_monitor.
1. `sensors` section, that define an array of sensors you want monitor
  1. Sensor properties, like IP, SNMP community, timeouts, etc
  1. Sensor monitors, actual stuff that we want to monitor and send

### Simple SNMP monitoring
The easiest way to start with rb_monitor is to monitor simple SNMP parameters, like load average, CPU and memory, and send them via kafka. This example configuration file does so:
```json
{
  "conf": {
    "debug": 2, /* See syslog error levels */
    "stdout": 1,
    "timeout": 1,
    "sleep_main_thread": 10,
    "sleep_worker_thread": 10,
    "kafka_broker": "192.168.101.201", /* Or your own kafka broker */
    "kafka_topic": "rb_monitor", /* Or the topic you desire */
  },
  "sensors": [
    {
      "sensor_id":1,
      "timeout":2000, /* Time this sensor has to answer */
      "sensor_name": "my-sensor", /* Name of the sensor you are monitoring*/
      "sensor_ip": "192.168.101.201", /* Sensor IP to send SNMP requests */
      "snmp_version": "2c",
      "community" : "redBorder", /* SNMP community */
      "monitors": [
        /* OID extracted from http://www.debianadmin.com/linux-snmp-oids-for-cpumemory-and-disk-statistics.html */

        {"name": "load_5", "oid": "UCD-SNMP-MIB::laLoad.2", "unit": "%"},
        {"name": "load_15", "oid": "UCD-SNMP-MIB::laLoad.3", "unit": "%"},
        {"name": "cpu_idle", "oid":"UCD-SNMP-MIB::ssCpuIdle.0", "unit":"%"},

        {"name": "memory_total", "nonzero":1, "oid": "UCD-SNMP-MIB::memTotalReal.0"},
        {"name": "memory_free",  "nonzero":1, "oid": "UCD-SNMP-MIB::memAvailReal.0"},

        {"name": "swap_total", "oid": "UCD-SNMP-MIB::memTotalSwap.0", "send":0},
        {"name": "swap_free",  "oid": "UCD-SNMP-MIB::memAvailSwap.0", "send":0 }
      ]
    }
  ]
}
```

This way, rb_monitor will send SNMP requests to obtain this information. If you read the kafka topic, you will see:
```json
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"load_5", "value":"0.100000", "type":"snmp", "unit":"%"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"load_15", "value":"0.100050", "type":"snmp", "unit":"%"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"cpu_idle", "value":"0.100000", "type":"snmp", "unit":"%"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"memory_total", "value":"256.000000", "type":"snmp"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"memory_free", "value":"120.000000", "type":"snmp"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"swap_total", "value":"0.000000", "type":"snmp"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"swap_free", "value":"0.000000", "type":"snmp"}
```

### Operation on monitors
The previous example is OK, but we can do better: What if I want the used CPU, or to know fast the % of the memory I have occupied? We can do operations on monitors (note: from now on, I will only put the monitors array, since the conf section is irrelevant):

```json
"monitors": [
  {"name": "load_5", "oid": "UCD-SNMP-MIB::laLoad.2", "unit": "%"},
  {"name": "load_15", "oid": "UCD-SNMP-MIB::laLoad.3", "unit": "%"},
  {"name": "cpu_idle", "oid":"UCD-SNMP-MIB::ssCpuIdle.0", "unit":"%", "send":0},
  {"name": "cpu", "op": "100-cpu_idle", "unit": "%"},


  {"name": "memory_total", "nonzero":1, "oid": "UCD-SNMP-MIB::memTotalReal.0", "send":0},
  {"name": "memory_free",  "nonzero":1, "oid": "UCD-SNMP-MIB::memAvailReal.0", "send":0},
  {"name": "memory", "op": "100*(memory_total-memory_free)/memory_total", "unit": "%", "kafka": 1 },
]
```

This way, rb_monitor will send SNMP requests to obtain this information. If you read the kafka topic, you will see:
```json
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"load_5", "value":"0.100000", "type":"snmp", "unit":"%"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"load_15", "value":"0.100050", "type":"snmp", "unit":"%"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"cpu", "value":"5.100000", "type":"op", "unit":"%"}
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"memory", "value":"20.000000", "type":"snmp"}
```

Here we got, we can do operations over previous monitor values, so we can get complex result from simpler values.

### System requests
You can't monitor everything using SNMP. We could add here telnet, HTTP REST interfaces, and a lot of complex stuffs. But, for now, we have the possibility of run a console command from rb_monitor, and to get result. For example, if you want to get the latency to reach some destination, you can add this monitor:
```json
"monitors"[
  {"name": "latency"  , "system": "nice -n 19 fping -q -s managerPro2 2>&1| grep 'avg round trip time'|awk '{print $1}'", "unit": "ms"}
]
```
And it will send to kafka this message:
```json
{"timestamp":1469183485,"sensor_name":"my-sensor","monitor":"latency","value":"0.390000","type":"system","unit":"ms"}
```
Notes:

1. Command are executed in the host running rb_monitor, so you can't execute remote commands this way. However, you can use ssh or telnet inside the system parameter
1. The shell used to run the command is the user's one, so take care if you use bash commands in dash shell, and stuffs like that.

### Vectors monitors
If you need to monitor same property on many instances (for example, received bytes of an interface), you can use vectors. You can return many values using a split token and then mix all them. For example, using `echo` instead of a proper program:

```json
"monitors"[
  {"name": "packets_received"  , "system": "echo '1;2;3'", "unit": "pkts", "split":";","instance_prefix": "interface-", "name_split_suffix":"_per_interface"}
]
```
And it will send to kafka this messages:
```json
{"timestamp":1469184314,"sensor_name":"my-sensor","monitor":"packets_received_per_interface","instance":"interface-0","value":1,"type":"system","unit":"pkts"}
{"timestamp":1469184314,"sensor_name":"my-sensor","monitor":"packets_received_per_interface","instance":"interface-1","value":2,"type":"system","unit":"pkts"}
{"timestamp":1469184314,"sensor_name":"my-sensor","monitor":"packets_received_per_interface","instance":"interface-2","value":3,"type":"system","unit":"pkts"}
```

If you want to calculate the sum off all packets (or the mean of another monitor), you can add `"split_op":"sum"` (or `"split_op":"mean"`) to the monitor and it will also send this last message:
```json
{"timestamp":1469184314,"sensor_name":"my-sensor","monitor":"packets_received","value":6,"type":"system","unit":"pkts"}
```

### Operations of vectors
If you have two vector monitors, you can operate on them as same as you do with scalar monitors.

Please note that If you do this kind of operation, it will apply for each vector element, but not to split operation result. But you can still do an split operation over the result (`sum` or `mean`) if you need that.

Blanks are handled this way: If one of the vector has a blank element, it is assumed as 0, for operation result and for split operation result.

### Sending custom data in messages
You can send attach any information you want in sent monitors if you use `enrichment` keyword, and adding an object. If you add it to a sensor, all monitors will be enrichment with that information; if you add it to a monitor, only that monitor will be enriched with the new JSON object.

For example, you can do:

```json
{
  "conf": {
    "debug": 2, /* See syslog error levels */
    "stdout": 1,
    "timeout": 1,
    "sleep_main_thread": 10,
    "sleep_worker_thread": 10,
    "kafka_broker": "192.168.101.201", /* Or your own kafka broker */
    "kafka_topic": "rb_monitor", /* Or the topic you desire */
    "enrichment":{ "my custom key":"my custom value" }
  },
  "sensors": [
    {
      "sensor_id":1,
      "timeout":2000, /* Time this sensor has to answer */
      "sensor_name": "my-sensor", /* Name of the sensor you are monitoring*/
      "sensor_ip": "192.168.101.201", /* Sensor IP to send SNMP requests */
      "snmp_version": "2c",
      "community" : "redBorder", /* SNMP community */
      "monitors": [
        /* OID extracted from http://www.debianadmin.com/linux-snmp-oids-for-cpumemory-and-disk-statistics.html */

        {"name": "load_5", "oid": "UCD-SNMP-MIB::laLoad.2", "unit": "%"},
        {"name": "load_15", "oid": "UCD-SNMP-MIB::laLoad.3", "unit": "%"},
        {"name": "cpu_idle", "oid":"UCD-SNMP-MIB::ssCpuIdle.0", "unit":"%", "enrichment": {"my-favourite-monitor":true}}
      ]
    }
  ]
}
```

And the kafka output will be:
```json
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"load_5", "value":"0.100000", "type":"snmp", "unit":"%","my custom key":"my custom value"  }
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"load_15", "value":"0.100050", "type":"snmp", "unit":"%","my custom key":"my custom value" }
{"timestamp":1469181339, "sensor_name":"my-sensor", "monitor":"cpu_idle", "value":"0.100000", "type":"snmp", "unit":"%","my custom key":"my custom value", "my-favourite-monitor":true}
```

### HTTP output
If you want to send the JSON directly via HTP POST, you can use this conf properties:
```json
"conf": {
  ...
  "http_endpoint": "http://localhost:8080/monitor",
  ...
}
```

Note that you need to configure with `--enable-http`

### SNMP traps
To receive SNMP traps you have to use this config properties:
```json
"conf": {
  ...
  "snmp_traps":{"server_name":"$snmp_server"}
}
```

And v1 and v2c SNMP traps will be sent using this message format:
```json
{"timestamp":1515352831,"monitor":"SNMPv2-SMI::enterprises.8072.2.3.0.1.0.17","value":"1.000000","sensor_name":"UDP: [172.18.0.1]:54946->[172.18.0.4]:162","SNMPv2-MIB::sysLocation.0":"Just here"}
{"timestamp":1515352886,"monitor":"SNMPv2-SMI::enterprises.8072.2.3.0.1","value":"1.000000","sensor_name":"UDP: [172.18.0.1]:50299->[172.18.0.4]:162","SNMPv2-SMI::enterprises.8072.2.3.2.1":123456}
```


## Installation

Just use the well known `./configure && make && make install`. You can see
configure options with `configure --help`. The most important are:

* `--enable-zookeeper`, that allows to get monitors requests using zookeeper
* `--enable-rbhttp`, to send monitors via HTTP POST instead of kafka.

### Dependencies
In order to compile `rb_monitor` you need to satisfy these dependencies:
- librd
- librdkafka
- json_c
- libmatheval
- net_snmp

`configure` script can download and install it for you if you use `--bootstrap`
option, except for librdkafka, but then you need these (more commons) deps:

- *librd*: C standard lib devel (for phtreads and librt)
- *libmatheval*: flex and bison. If you don't have flex, it will be bootstrapped
  too, but you need `m4`

## Docker
You can generate a development docker container with `make dev-docker`, with a
ready to use environment to compile and test rb_monitor. Also, you can generate
a release docker container with `make docker`, but make sure that you compiled
properly `rb_monitor`, i.e., with optimizations, no coverage generation
information, etc etc.

## TODO
- [ ] Vector <op> scalar operation (see #14 )
- [ ] SNMP tables / array (see #15 )
