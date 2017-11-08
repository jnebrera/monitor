from multiprocessing import Process, Barrier

from pysnmp.entity import engine, config
from pysnmp.entity.rfc3413 import cmdrsp, context
from pysnmp.carrier.asynsock.dgram import udp
from pysnmp.smi import instrum
from pysnmp.proto.api import v2c


class SNMPAgentResponder(instrum.AbstractMibInstrumController):
    ''' Responder to use in SNMPAgent.'''
    OID_PREFIX = (2, 25)

    def __init__(self, port, responses):
        ''' Constructor. Arguments:
        - responses: oid->response map'''
        self.__port = port
        self.__responses = {
            SNMPAgentResponder.OID_PREFIX + key: response
            for key, response in responses.items()
        }

    def readVars(self, vars, acInfo=(None, None)):
        return [(oid, self.__responses[oid]) for oid, _ in vars]


class SNMPAgent(Process):
    ''' Execute a SNMP agent Process'''
    def __init__(self, port, responder):
        Process.__init__(self, daemon=True)
        timeout_s = 5

        self.__listening_port = port
        self.__responder = responder
        self.__barrier = Barrier(parties=2, timeout=timeout_s)

    def run(self):
        snmpEngine = engine.SnmpEngine()

        config.addSocketTransport(
            snmpEngine,
            udp.domainName,
            udp.UdpTransport().openServerMode(('127.0.0.1',
                                               self.__listening_port))
        )

        config.addV1System(
                     snmpEngine, 'my-area', 'public', contextName='my-context')

        config.addVacmUser(snmpEngine=snmpEngine,
                           securityModel=2,
                           securityName='my-area',
                           securityLevel='noAuthNoPriv',
                           readSubTree=SNMPAgentResponder.OID_PREFIX,
                           writeSubTree=(),
                           notifySubTree=())

        snmpContext = context.SnmpContext(snmpEngine)

        snmpContext.registerContextName(
            v2c.OctetString('my-context'),         # Context Name
            self.__responder                       # Management Instrumentation
        )

        cmdrsp.GetCommandResponder(snmpEngine, snmpContext)

        snmpEngine.transportDispatcher.jobStarted(1)
        self.__barrier.wait()

        # TODO with statement here!
        try:
            snmpEngine.transportDispatcher.runDispatcher()
        except:
            snmpEngine.transportDispatcher.closeDispatcher()
            raise

    def __enter__(self):
        self.start()
        self.__barrier.wait()
        return self

    def __exit__(self, type, value, traceback):
        self.terminate()
