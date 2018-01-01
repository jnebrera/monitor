#!/usr/bin/env python3

from mon_test import TestMonitor, main
import pytest
import enum

from pysnmp.proto import api
from pysnmp.smi.rfc1902 import ObjectIdentity

import itertools


class TestTraps(TestMonitor):
    __trapStrToOid = {
        'coldStart': (1, 3, 6, 1, 6, 3, 1, 1, 5, 1),
        'linkUp':    (1, 3, 6, 1, 6, 3, 1, 1, 5, 4),
    }

    def base_test_trap(self, messages, child, kafka_handler):
        base_config = {'conf': {'snmp_traps': {}}}

        t_locals = locals()
        self.base_test(child_argv_str=t_locals['child'],
                       snmp_responses=None,
                       **{key: t_locals[key] for key in ['base_config',
                                                         'kafka_handler',
                                                         'messages']})

    @pytest.mark.parametrize("snmp_version,enterprise_trap_oid",
                             [(snmp_v, enterprise_oid)
                              for snmp_v in [api.protoVersion1,
                                             api.protoVersion2c]
                              for enterprise_oid
                              in [None,
                                  (1, 3, 6, 1, 1, 2, 3, 4, 1),
                                  (1, 3, 6, 1, 1, 2, 3, 4, 1, 0)]
                              if not (snmp_v == api.protoVersion2c and
                              enterprise_oid)])
    def test_trap(self,
                  child,
                  kafka_handler,
                  snmp_version,
                  enterprise_trap_oid):
        snmp_mod = api.protoModules[snmp_version]

        ifIndexOid = (1, 3, 6, 1, 2, 1, 2, 2, 1, 1)
        ifDescriptionOid = ifIndexOid[:-1] + (2,)  # DisplayString

        parameters = [
          {
              'proto_version': snmp_version,
              'enterprise_trap_oid': enterprise_trap_oid,
              'generic_trap_oid': generic_trap_oid
              if not enterprise_trap_oid else 'enterpriseSpecific',
              'var_binds': var_binds,
              'expected_kafka_message': kafka_messages,
          } for (generic_trap_oid, var_binds, kafka_messages)
          in [
            # SNMP trap with no variables: Send default value 1
            ('coldStart',
             None, {'monitor': 'SNMPv2-MIB::coldStart',
                    'value': '1.000000'}),
            # SNMP v1 trap with interface variable.
            ('linkUp',
             [(ifIndexOid + (1,), snmp_mod.null)],
             {'monitor': 'IF-MIB::linkUp',
              'value': '1.000000',
              'if_index': '1'}),
            # SNMP trap with interface and many variable types.
            ('linkUp',
             [(ifIndexOid + (1,), snmp_mod.null),
              # number
              (ifDescriptionOid + (1,), snmp_mod.OctetString('My interface'))],
             {'monitor': 'IF-MIB::linkUp',
              'value': '1.000000',
              'if_index': '1',
              'IF-MIB::ifDescr.1': 'My interface'}),
          ]
        ]  # End of parameters

        kafka_messages = [p['expected_kafka_message'] for p in parameters]
        # Sanitize enterprise specific monitors
        if enterprise_trap_oid:
            for kafka_message in kafka_messages:
                kafka_message['monitor'] = 'SNMPv2-SMI::directory.2.3.4.1.0.0'

        messages = [{
            'snmp_trap': self.__trap_message(**{
                                              k: v for (k, v) in p.items()
                                              if k != 'expected_kafka_message'
                                             }),
            'kafka_messages': [kafka_message]}
            for (p, kafka_message) in zip(parameters, kafka_messages)]

        self.base_test_trap(messages, child, kafka_handler)

    def test_malformed_v2_traps(self, child, kafka_handler):
        pMod = api.protoModules[api.protoVersion2c]

        compress_selectors = [(timestamp_var, oid_var)
                              for timestamp_var in (True, False)
                              for oid_var in (True, False)
                              if not all((timestamp_var, oid_var))]

        trapPDUs = [pMod.TrapPDU() for elm in compress_selectors]
        for trapPDU, compress_selector in zip(trapPDUs, compress_selectors):
            pMod.apiTrapPDU.setDefaults(trapPDU)

            var_binds = pMod.apiTrapPDU.getVarBinds(trapPDU)
            pMod.apiTrapPDU.setVarBinds(trapPDU,
                                        itertools.compress(var_binds,
                                                           compress_selector))

        trapMsgs = [pMod.Message() for elm in trapPDUs]
        for (trapMsg, trapPDU) in zip(trapMsgs, trapPDUs):
            pMod.apiMessage.setDefaults(trapMsg)
            pMod.apiMessage.setCommunity(trapMsg, 'public')
            pMod.apiMessage.setPDU(trapMsg, trapPDU)

        # We should only receive messages in traps with oid. No oid -> fail
        # Note: SNMPv2-MIB::coldStart is the default oid.
        messages = [{
            'snmp_trap': trap_msg,
            'kafka_messages': [{'monitor': 'SNMPv2-MIB::coldStart',
                                'value': '1.000000'}]
                    if compress_selector[1] else None
                    }
                    for (compress_selector, trap_msg)
                    in zip(compress_selectors, trapMsgs)]

        self.base_test_trap(messages, child, kafka_handler)

    def __trap_message(self,
                       proto_version,
                       enterprise_trap_oid,
                       generic_trap_oid,
                       var_binds=None):
        ''' Construct parametrized SNMP message'''
        # Protocol version to use
        pMod = api.protoModules[proto_version]

        # Build PDU
        trapPDU = pMod.TrapPDU()
        pMod.apiTrapPDU.setDefaults(trapPDU)

        # Traps have quite different semantics across proto versions
        if proto_version == api.protoVersion1:
            if enterprise_trap_oid:
                pMod.apiTrapPDU.setGenericTrap(trapPDU, 'enterpriseSpecific')
                pMod.apiTrapPDU.setEnterprise(trapPDU, enterprise_trap_oid)
            else:
                pMod.apiTrapPDU.setGenericTrap(trapPDU, generic_trap_oid)

        if var_binds:
            var_bind_list = pMod.apiTrapPDU.getVarBinds(trapPDU) + var_binds
            if proto_version == api.protoVersion2c:
                try:
                    var_bind_list[1] = (var_bind_list[1][0],
                                        pMod.ObjectIdentifier(
                                            TestTraps.__trapStrToOid[
                                                            generic_trap_oid]))
                except KeyError:
                    pass
            pMod.apiTrapPDU.setVarBinds(trapPDU, var_bind_list)

        # Build message
        trapMsg = pMod.Message()
        pMod.apiMessage.setDefaults(trapMsg)
        pMod.apiMessage.setCommunity(trapMsg, 'public')
        pMod.apiMessage.setPDU(trapMsg, trapPDU)

        return trapMsg

if __name__ == '__main__':
    main()
