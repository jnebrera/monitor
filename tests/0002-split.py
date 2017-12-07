#!/usr/bin/env python3

from mon_test import TestMonitor, main
from pysnmp.proto.api import v2c
from itertools import chain, product
import pytest


@pytest.fixture(params=[None, '_per_instance'])
def name_split_suffix(request):
    return request.param


@pytest.fixture(params=[None, 'load-instance-'])
def instance_prefix(request):
    return request.param

VALID_SPLIT_OPS = ['sum', 'mean']

# @pytest.fixture(params=[None, 'sum', 'avg', 'mean'])
@pytest.fixture(params=[None, 'invalid'] + VALID_SPLIT_OPS)
def split_op(request):
    return request.param


class TestSplit(TestMonitor):
    ''' Test for split behavior '''
    def test_split(self,
                   child,
                   name_split_suffix,
                   instance_prefix,
                   split_op,
                   kafka_handler):
        ''' Test for array split '''
        load_1 = [3, 2, 1, 0]
        load_5 = [4, 5, 6, 7]

        tests_arrays = [load_1, load_5]

        split_tok = ';'

        # Configuration in sensor/monitor that must be forwarded to kafka
        # messages
        sensor_config_base = {'sensor_id': 1,
                              'sensor_name': 'sensor-test-01'}

        # Add requested keys to monitor
        t_locals = locals()
        monitor_base = {
            k: t_locals.get(k) for k in ['split_tok',
                                         'name_split_suffix',
                                         'instance_prefix',
                                         'split_op',
                                         ]
            if t_locals.get(k) is not None
        }

        sensor_config = {
            **sensor_config_base,
            'timeout': 100000000,
            'community': 'public',
            'monitors': [{
                **monitor_base,
                'name': 'array_{}'.format(monitor_i),
                'system': "echo -n '{}'".format(
                                         ';'.join(str(i) for i in test_array)),
                'split': split_tok,
            } for (monitor_i, test_array) in enumerate(tests_arrays)]
        }

        base_config = {'sensors': [sensor_config]}

        # Kafka messages per monitor
        kafka_messages = [
            [{
                **{'instance': '{}{}'.format(instance_prefix, value_i)
                               if instance_prefix else None},
                'type': 'system',
                'sensor_id': 1,
                'sensor_name': 'sensor-test-01',
                'monitor': 'array_{}{}'.format(monitor_i,
                                               name_split_suffix or ''),
                'value': '{:.6f}'.format(value)
             } for value_i, value in enumerate(test_array)
             ] + [{
                'type': 'system',
                'sensor_id': 1,
                'sensor_name': 'sensor-test-01',
                'monitor': 'array_{}'.format(monitor_i),
                'instance': None,
                'value': '{:.6f}'.format(
                    (sum if split_op == 'sum'
                         else lambda x: float(sum(x))/len(x))(test_array))
             }] if split_op in VALID_SPLIT_OPS else []
            for (monitor_i, test_array) in enumerate(tests_arrays)]

        # Flatten kafka messages
        kafka_messages = sum(kafka_messages, [])

        t_locals = locals()
        self.base_test(child_argv_str=t_locals['child'],
                       snmp_responses=None,
                       **{key: t_locals[key] for key in ['base_config',
                                                         'kafka_handler',
                                                         'kafka_messages']})

if __name__ == '__main__':
    main()
