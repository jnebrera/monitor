#!/usr/bin/env python3

from mon_test import TestMonitor, main
from pysnmp.proto.api import v2c
import pytest
import enum


class BaseTestType(enum.Enum):
    SNMP = enum.auto()
    SYSTEM = enum.auto()


@pytest.fixture(params=iter(BaseTestType))
def monitor_type(request):
    return request.param


class TestBasic(TestMonitor):

    def test_base_monitor(self,
                          monitor_type,
                          child,
                          kafka_handler):
        ''' First monitor tests. It expects n monitors, via SNMP or system
        console call. Each monitor is called "mon_$i", and returns "$i", where
        i is a correlative number between 0 and the number of desired monitors.

        Also, last monitor will bring NO unit.

        Arguments:
            monitor_type:  BaseTestType (SNMP or SYSTEM) of test
            child:         Child to test with.
            kafka_handler: Kafka handler to execute test with.
        '''

        # TODO: Test all these v2c format
        # TODO: Test string,bits...->number conversions!
        class SNMPTypes(enum.Enum):
            Integer = v2c.Integer
            Integer32 = v2c.Integer32
            OctetString = v2c.OctetString
            # IpAddress = v2c.IpAddress
            # Counter32 = v2c.Counter32
            Gauge32 = v2c.Gauge32
            Unsigned32 = v2c.Unsigned32
            # TimeTicks = v2c.TimeTicks
            # Counter64 = v2c.Counter64
            # Bits = v2c.Bits

            # For no-unit testing
            OctetStringNoUnit = v2c.OctetString

            # For no-value testing. Keep it last member!
            OctetStringEmpty = v2c.OctetString

        # __members__ allow to count duplicates
        n_monitors = len(SNMPTypes.__members__)
        kafka_message_type, monitors_operation_key, monitors_operation_values \
            = \
            ('snmp', 'oid', {(0, i): v2c_type.value('' if i == n_monitors-1
                                                    else str(i))
                             for i, v2c_type
                             in enumerate(SNMPTypes.__members__.values())}) \
            if monitor_type is BaseTestType.SNMP else \
            ('system', 'system', ['echo {}'.format('-n' if i == n_monitors-1
                                                   else i)
                                  for i in range(n_monitors)])

        sensor_config = {
            'sensor_id': 1,
            'timeout': 100000000,
            'sensor_name': 'sensor-test-01',
            'community': 'public',
            'monitors': [
                # Two monitors of simulated system load
                {'name': 'monitor_' + str(i),
                 monitors_operation_key: operation_val,
                 'integer': 1,
                 'unit': '%',
                 } for i, operation_val in enumerate(monitors_operation_values)
            ]
        }

        kafka_messages = [{'type': kafka_message_type,
                           'sensor_id': 1,
                           'sensor_name': 'sensor-test-01',
                           'unit': '%',
                           'monitor': 'monitor_' + str(i),
                           'value': '{:6f}'.format(i)
                           } for i in range(n_monitors)]
        messages = [{'kafka_messages': kafka_messages}]

        # Test with no unit
        del sensor_config['monitors'][-2]['unit']
        kafka_messages[-2]['unit'] = None
        # Empty string test
        del kafka_messages[-1]

        base_config = {'sensors': [sensor_config]}

        t_locals = locals()
        self.base_test(child_argv_str=t_locals['child'],
                       snmp_responses=monitors_operation_values
                       if monitor_type is BaseTestType.SNMP else None,
                       **{key: t_locals[key] for key in ['base_config',
                                                         'kafka_handler',
                                                         'messages']})


if __name__ == '__main__':
    main()
