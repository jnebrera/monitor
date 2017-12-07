#!/usr/bin/env python3

from mon_test import TestMonitor, main
from pysnmp.proto.api import v2c
from itertools import chain
import pytest


@pytest.fixture(params=[False, True])
def send_base_vars(request):
    return request.param


class TestBasic(TestMonitor):
    ''' First monitor tests'''
    def test_base(self,
                  child,
                  send_base_vars,
                  kafka_handler):
        ''' Test for math operations '''
        n0, n1 = 3, 5
        test_numbers = (n0, n1)
        eval_globals = None
        eval_locals = {'var_n0': n0, 'var_n1': n1}

        test_parameters = [{
                # Variables gathering
                'monitor_name': 'var_n' + str(i),
                'monitor_op_key': 'oid',
                'monitor_op_arg': (1, i),
                'monitor_expected_value': number,
                'send': send_base_vars,
            } for i, number in enumerate(test_numbers)
        ] + [{
                # Valid operations
                'monitor_name': 'op_' + operation,
                'monitor_op_key': 'op',
                'monitor_op_arg': operation,
                'monitor_expected_value': eval(str.replace(operation,
                                                           '^',
                                                           '**'),
                                               eval_globals,
                                               eval_locals),
            } for operation in chain(
                # Test all libmatheval supported binary operations
                ('var_n0' + operator + 'var_n1' for operator in '+-*/^'),
                # Test operation on constants
                iter(['100*var_n1', '-var_n1']),
                # TODO
                # Test operations with no variables involved
                # iter(['2'])
                )
        ] + [{
                # Invalid operations (No 'monitor_expected_value')
                'monitor_name': 'op_' + operation,
                'monitor_op_key': 'op',
                'monitor_op_arg': operation,
            } for operation in ['0*var_n1', 'var_n1/0', 'log(0)', 'sqrt(-1)',
                                '2*unknown_variable']
        ]

        snmp_responses = {
            monitor['monitor_op_arg']:
                v2c.OctetString(str(monitor['monitor_expected_value']))
            for monitor in test_parameters
            if monitor['monitor_op_key'] == 'oid'
        }

        # Configuration in sensor/monitor that must be forwarded to kafka
        # messages
        sensor_config_base = {'sensor_id': 1,
                              'sensor_name': 'sensor-test-01'}

        sensor_config = {
            **sensor_config_base,
            **{
                'timeout': 100000000,
                'community': 'public',
                'monitors': [{
                    'send': parameter.get('send', True),
                    'name': parameter['monitor_name'],
                    parameter['monitor_op_key']: parameter['monitor_op_arg'],
                } for parameter in test_parameters]
            }
        }

        base_config = {'sensors': [sensor_config]}

        kafka_messages = [{
            'type':
                'snmp' if parameter['monitor_op_key'] == 'oid'
                else parameter['monitor_op_key'],
            'sensor_id': 1,
            'sensor_name': 'sensor-test-01',
            'monitor': parameter['monitor_name'],
            'value': '{:.6f}'.format(parameter['monitor_expected_value'])
        } for parameter in test_parameters
          if 'monitor_expected_value' in parameter and parameter.get('send')
        ]

        t_locals = locals()
        self.base_test(child_argv_str=t_locals['child'],
                       **{key: t_locals[key] for key in ['base_config',
                                                         'kafka_handler',
                                                         'kafka_messages',
                                                         'snmp_responses']})

if __name__ == '__main__':
    main()
