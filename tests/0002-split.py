#!/usr/bin/env python3

from mon_test import TestMonitor, main, valgrind_handler
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
# TODO: VALID_OPERATORS = '+-*/^'
VALID_OPERATORS = '+-*/'


@pytest.fixture(params=[None, 'invalid'] + VALID_SPLIT_OPS)  # TODO mean
def split_op(request):
    return request.param

# Check for holes AND zero values, they don't mean the same because of the
# average calculation!
@pytest.fixture(params=[([3, 2, 1, 0], [4, 5, 6, 7]),
                        ([None, 2, 1, 0], [None, 6, 8, 10]),
                        ([None, 2, 1, 0], [4, 6, None, 10]),
                        ([None, None, None, None], [None, None, None, None])])
def tests_arrays(request):
    return request.param


class TestSplit(TestMonitor):
    ''' Test for split behavior '''
    def test_split(self,
                   child,
                   tests_arrays,
                   name_split_suffix,
                   instance_prefix,
                   split_op,
                   kafka_handler,
                   valgrind_handler):
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

        operations = chain(
            # Test all libmatheval supported binary operations and do all
            # operations on result
            product(('array_0' + str.replace(operator, '^', '**') + 'array_1'
                     for operator in VALID_OPERATORS),
                    VALID_SPLIT_OPS),
            # TODO
            # Test operation on constants
            # iter(['100*array_1', '-array_1']),
            # Test variable(op)array
            # iter(['x+array'])
            # Test const(op)array
            # iter([2+array])
            )

        # Need to make a list in order to avoid generation exhaustion
        operations = [('op_{}_{}'.format(operation, result_op),
                       operation,
                       result_op) for (operation, result_op) in operations]

        monitors_config = [{
                **monitor_base,
                'name': 'array_{}'.format(monitor_i),
                'system': "echo -n '{}'".format(
                                         ';'.join(str(i) if i is not None
                                                  else ''
                                                  for i in test_array)),
                'split': split_tok,
            } for (monitor_i, test_array) in enumerate(tests_arrays)
        ] + [
            {
                **{'split_op': v for v in (result_split_op)
                    if result_split_op != ' '},
                'name_split_suffix': name_split_suffix,
                'instance_prefix': instance_prefix,
                'name': name,
                'op': operation,
            } for (name, operation, result_split_op) in operations
            if (name_split_suffix and instance_prefix)
        ]

        sensor_config = {
            **sensor_config_base,
            'timeout': 100000000,
            'community': 'public',
            'monitors': monitors_config,
        }

        base_config = {'sensors': [sensor_config]}

        eval_globals = None

        def average(x): return float(sum(x))/len(x) if len(x) > 0 else 0

        # Kafka messages per monitor
        kafka_messages = [
            # System arrays & operations over array
            [{
                **{'instance': '{}{}'.format(instance_prefix, value_i)
                               if instance_prefix else None},
                'type': 'system',
                'sensor_id': 1,
                'sensor_name': 'sensor-test-01',
                'monitor': 'array_{}{}'.format(monitor_i,
                                               name_split_suffix or ''),
                'value': '{:.6f}'.format(value)
             } for value_i, value in enumerate(test_array) if value is not None
             ] + ([{
                'type': 'system',
                'sensor_id': 1,
                'sensor_name': 'sensor-test-01',
                'monitor': 'array_{}'.format(monitor_i),
                'instance': None,
                'value': '{:.6f}'.format(
                    (sum if split_op == 'sum'
                         else average)([x for x in test_array
                                        if x is not None]))
             }] if split_op in VALID_SPLIT_OPS else [])
            for (monitor_i, test_array) in enumerate(tests_arrays)
        ] + [
            # Operations over arrays & result operations
            [{
                'instance': '{}{}'.format(instance_prefix, monitor_i),
                'type': 'op',
                'sensor_id': 1,
                'sensor_name': 'sensor-test-01',
                'monitor': '{}{}'.format(name, name_split_suffix or ''),
                'value': eval(
                    '"{:6f}".format('+operation.replace('^', '**')+')',
                    eval_globals,
                    {'array_0': tests_arrays[0][monitor_i],
                     'array_1': tests_arrays[1][monitor_i]})
            } for ((name, operation, result_split_op), monitor_i)
              in product(operations, range(len(tests_arrays[0])))
              if tests_arrays[0][monitor_i] is not None and
              tests_arrays[1][monitor_i] is not None]
        ] if (name_split_suffix and instance_prefix) else []

        # Flatten kafka messages
        kafka_messages = sum(kafka_messages, [])

        # Filter operation kafka messages with 0 values, monitor will never
        # send them.
        kafka_messages = [m for m in kafka_messages
                          if (m['type'] == 'system' and
                              m['instance'] is not None) or
                          '0.000000' != m['value']]
        messages = [{'kafka_messages': kafka_messages}]

        t_locals = locals()
        self.base_test(child_argv_str=t_locals['child'],
                       snmp_responses=None,
                       **{key: t_locals[key] for key in ['base_config',
                                                         'kafka_handler',
                                                         'messages',
                                                         'valgrind_handler']})

if __name__ == '__main__':
    main()
