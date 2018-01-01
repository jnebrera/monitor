#!/usr/bin/env python3

from tempfile import NamedTemporaryFile
from socket import socket, AF_INET, SOCK_STREAM, SOCK_DGRAM
from subprocess import Popen, PIPE
import os
import signal
import pytest
import json
import contextlib
import select

from snmp_agent import SNMPAgent, SNMPAgentResponder
from pysnmp.carrier.asyncore.dispatch import AsyncoreDispatcher
from pysnmp.carrier.asyncore.dgram import udp
from pyasn1.codec.ber import encoder
from mon_test_kafka import KafkaHandler


class MonitorChild(Popen):
    ''' Wrapper class for popen a monitor child '''
    def __init__(self, args, wait_for_traps_listener_port=0, **kwargs):
        ''' Creates child and (optionally) waits for trap listener ready '''
        Popen.__init__(self, args,
                       **{**kwargs, 'stdout': PIPE, 'stderr': PIPE})
        if wait_for_traps_listener_port > 0:
            self.__wait_for_trap_listener(
                                    expected_port=wait_for_traps_listener_port)

    def __wait_for_trap_listener(self, expected_port):
        ''' Wait for trap listener ready message '''
        MESSAGE_START = b" Listening for traps on localhost:"

        for message in self.__messages(t_timeout_ms=60000):
            if not message.startswith(MESSAGE_START):
                continue

            port = int(message[len(MESSAGE_START):])

            assert(port == expected_port)
            break

    def __messages(self, t_timeout_ms):
        ''' Obtain one monitor stdout message '''
        poll_handler = select.poll()
        poll_handler.register(self.stdout, select.POLLIN)

        LOG_INDEX = 4
        while True:
            ready_fd = [fd for fd, _ in poll_handler.poll(t_timeout_ms)]
            if not ready_fd:
                return

            line = self.stdout.readline()

            # Skip log timestamp, function,...
            yield line.split(b'|')[LOG_INDEX]


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


class SNMPTrapsDispatcher(AsyncoreDispatcher):
    def __enter__(self):
        self.registerTransport(udp.domainName,
                               udp.UdpSocketTransport().openClientMode())
        return self

    def __exit__(self, type, value, traceback):
        self.closeDispatcher()

    def send_message(self,
                     trap_port,
                     snmp_trap):
        self.sendMessage(
            encoder.encode(snmp_trap), udp.domainName, ('localhost', trap_port)
        )

        self.runDispatcher()


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
        'debug': 6,
        'timeout': 1,
        'sleep_main_thread': 100000,
        'sleep_worker_thread': 25,
        'kafka_broker': 'kafka'
    }

    def __create_config_file(t_test_config):
        ''' Creates config file using template test config.
        Returns a tuple with the file_name and the dict with JSON test_config
        '''
        try:
            t_test_config_conf = t_test_config['conf']
            # Use default config and override with defined ones
            t_test_config['conf'] = {**TestMonitor.__BASE_CONFIG_CONF,
                                     **t_test_config_conf}
        except KeyError:
            t_test_config['conf'] = TestMonitor.__BASE_CONFIG_CONF

        t_file_name = TestMonitor.__random_config_file()
        t_test_config['conf']['kafka_topic'] = TestMonitor.__random_topic()

        for sensor in t_test_config.get('sensors', []):
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

        if t_test_config['conf'].get('snmp_traps') is not None:
            t_test_config['conf']['snmp_traps']['server_name'] = \
                'localhost:{}'.format(TestBase.random_port(family=AF_INET,
                                                           type=SOCK_DGRAM))

        with open(t_file_name, 'w') as f:
            json.dump(t_test_config, f)
            return (t_file_name, t_test_config)

    def base_test(self,
                  base_config,
                  child_argv_str,
                  snmp_responses,
                  kafka_handler,
                  messages):
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

        config_file, config = TestMonitor.__create_config_file(base_config)
        try:
            snmp_agent_port = int(
                          base_config['sensors'][0]['sensor_ip'].split(':')[1])
        except KeyError:
            pass

        try:
            snmp_trap_port = base_config['conf']['snmp_traps']['server_name']
            snmp_trap_port = int(snmp_trap_port.split(':')[1])
        except KeyError:
            snmp_trap_port = 0

        kafka_topic = base_config['conf']['kafka_topic']

        child_argv = child_argv_str.split()
        if len(child_argv) == 0 or child_argv[-1] != './rb_monitor':
            child_argv.append('./rb_monitor')

        snmp_trap_dispatcher = None
        with contextlib.ExitStack() as exit_stack:
            if snmp_responses is not None:
                exit_stack.enter_context(
                    SNMPAgent(port=snmp_agent_port,
                              responder=SNMPAgentResponder(
                                                   port=snmp_agent_port,
                                                   responses=snmp_responses)))
            child = exit_stack.enter_context(
                     MonitorChild(args=child_argv + ['-c', config_file],
                                  wait_for_traps_listener_port=snmp_trap_port))

            try:
                for m in messages:
                    snmp_trap = m.get('snmp_trap')

                    if snmp_trap is not None:
                        if snmp_trap_dispatcher is None:
                            snmp_trap_dispatcher = exit_stack.enter_context(
                                                         SNMPTrapsDispatcher())
                        snmp_trap_dispatcher.send_message(
                                                      trap_port=snmp_trap_port,
                                                      snmp_trap=snmp_trap)
                    try:
                        if not m['kafka_messages']:
                            raise KeyError

                        t_test = MonitorKafkaMessages(
                                topic_name=kafka_topic,
                                expected_kafka_messages=m['kafka_messages'])
                        t_test.test(kafka_handler=kafka_handler)
                    except KeyError:
                        pass  # No messages given

            finally:
                child.send_signal(signal.SIGINT)
                timeout_s = 5
                child.wait(timeout_s)


def main():
    pytest.main()
