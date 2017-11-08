#!/usr/bin/env python3

from tempfile import NamedTemporaryFile
from snmp_agent import SNMPAgent, SNMPAgentResponder
from mon_test_kafka import KafkaHandler
from socket import socket, AF_INET, SOCK_STREAM, SOCK_DGRAM
from subprocess import Popen
import os
import signal
import pytest
import json


class MonitorKafkaMessages(object):
    ''' Base SNMP message for testing '''

    def __init__(self, topic_name, expected_kafka_messages):
        self.__topic_name = topic_name
        self.__messages = expected_kafka_messages

    def test(self, kafka_handler):
        ''' Do the SNMP message test.

        Arguments:
          - kafka handler:
        '''
        kafka_handler.check_kafka_messages(
                     check_messages_callback=KafkaHandler.assert_messages_keys,
                     topic_name=self.__topic_name,
                     messages=self.__messages)


class TestBase(object):
    ''' Base class for tests. Need to override the functions:
    - random_resource_prefix(): Return a string with a prefix for random
    resource creation
    '''
    def random_resource_file(random_resource_prefix, t_resource_name):
        resource_name_prefix = random_resource_prefix + '_' + t_resource_name \
                                                                          + '_'
        with NamedTemporaryFile(
                      prefix=resource_name_prefix, delete=False, dir='.') as f:
            return os.path.basename(f.name)

    def random_resource(random_resource_prefix, resource_name):
        ret = TestBase.random_resource_file(
                                 random_resource_prefix=random_resource_prefix,
                                 t_resource_name=resource_name)

        return ret[len(random_resource_prefix + '__' + resource_name):]

    def random_port(family=AF_INET, type=SOCK_STREAM):
        # Socket is closed, but child process should be able to open it while
        # TIMED_WAIT avoid other process to open it.
        with socket(family, type) as s:
            SOCKET_HOST = ''
            SOCKET_PORT = 0
            s.bind((SOCKET_HOST, SOCKET_PORT))
            name = s.getsockname()
            return name[1]

    def random_topic(random_resource_prefix):
        # kafka broker complains if topic ends with __
        topic = '_'
        while '_' in topic:
            topic = TestBase.random_resource(
                random_resource_prefix=random_resource_prefix,
                resource_name='topic')
        return topic


class TestMonitor(TestBase):
    def __random_resource_file(t_resource_name):
        return TestBase.random_resource_file('monitor', t_resource_name)

    def __random_resource(resource_name):
        return TestBase.random_resource('monitor', resource_name)

    def __random_topic():
        return TestBase.random_topic('monitor')

    def __random_config_file():
        return TestMonitor.__random_resource_file('config')

    __BASE_CONFIG_CONF = {
        'debug': 2,
        'timeout': 1,
        'sleep_main_thread': 100000,
        'sleep_worker_thread': 25,
        'kafka_broker': 'kafka'
    }

    def __create_config_file(self, t_test_config):
        try:
            t_test_config_conf = t_test_config['conf']
            # Use default config and override with defined ones
            t_test_config['conf'] = {**TestMonitor.__BASE_CONFIG_CONF,
                                     **t_test_config_conf}
            del t_test_config
        except KeyError:
            t_test_config['conf'] = TestMonitor.__BASE_CONFIG_CONF

        t_file_name = TestMonitor.__random_config_file()
        t_test_config['conf']['kafka_topic'] = TestMonitor.__random_topic()

        for sensor in t_test_config['sensors']:
            # Add expected snmp agent port
            try:
                snmp_agent_port = int(sensor['sensor_ip'].split(':')[1])
            except (KeyError, IndexError) as ex:
                snmp_agent_port = TestBase.random_port(
                                               family=AF_INET, type=SOCK_DGRAM)

                if isinstance(ex, KeyError):
                    sensor['sensor_ip'] = 'localhost'
                sensor['sensor_ip'] += ':' + str(snmp_agent_port)

            # Add randomness in oids
            for monitor in sensor['monitors']:
                try:
                    monitor_oid = monitor['oid']
                    if len(monitor_oid) < 2 or \
                            monitor_oid[:2] != SNMPAgentResponder.OID_PREFIX:
                        monitor_oid = SNMPAgentResponder.OID_PREFIX + \
                                      monitor_oid

                    monitor['oid'] = '.'.join(str(oid_node)
                                              for oid_node in monitor_oid)
                except KeyError:
                    pass  # Not oid

        with open(t_file_name, 'w') as f:
            json.dump(t_test_config, f)
            return (t_file_name, t_test_config)

    class BaseTestNoSNMPAgent(object):
        def __enter__(self):
            pass

        def __exit__(self, type, value, traceback):
            pass

    def base_test(self,
                  base_config,
                  child_argv_str,
                  snmp_responses,
                  kafka_handler,
                  kafka_messages):
        ''' Base monitor test

        Arguments:
          - base_config: Base config file to use. snmp agent port, number of
            threads (1), brokers (kafka) and some rdkafka options will be added
            if not present.
          - child_argv_str: Child string to execute. `-c <config> will be added
          - snmp_responses: Expected SNMP agent responses
          - messages: kafka messages to expect
          - kafka_handler: Kafka handler to use
        '''
        config_file, config = self.__create_config_file(base_config)
        snmp_agent_port = int(
                          base_config['sensors'][0]['sensor_ip'].split(':')[1])
        kafka_topic = base_config['conf']['kafka_topic']

        child_argv = child_argv_str.split()
        if len(child_argv) == 0 or child_argv[-1] != './rb_monitor':
            child_argv.append('./rb_monitor')

        with SNMPAgent(port=snmp_agent_port,
                       responder=SNMPAgentResponder(
                           port=snmp_agent_port, responses=snmp_responses)) \
                if snmp_responses is not None \
                else TestMonitor.BaseTestNoSNMPAgent(), \
                Popen(args=child_argv + ['-c', config_file]) as child:
            try:
                t_test = MonitorKafkaMessages(
                                    topic_name=kafka_topic,
                                    expected_kafka_messages=kafka_messages)
                t_test.test(kafka_handler=kafka_handler)
            finally:
                child.send_signal(signal.SIGINT)
                timeout_s = 5
                child.wait(timeout_s)


def main():
    pytest.main()
