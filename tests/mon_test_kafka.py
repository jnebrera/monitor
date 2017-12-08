from concurrent.futures import ThreadPoolExecutor, as_completed

import pykafka.common
import itertools
import json
from pykafka import KafkaClient


class KafkaHandler(object):
    ''' Kafka handler for n2kafka tests '''

    def __init__(self, hosts="kafka:9092"):
        self.__kafka = KafkaClient(hosts)
        self.__kafka_consumers = {}

    def __consumer(self, topic_name):
        try:
            return self.__kafka_consumers[topic_name]
        except KeyError as e:
            # Create topic consumer
            topic = self.__kafka.topics[topic_name]
            offset_earliest = pykafka.common.OffsetType.EARLIEST
            consumer = topic.get_simple_consumer(
                auto_offset_reset=offset_earliest,
                consumer_timeout_ms=60 * 1000)
            consumer.start()
            self.__kafka_consumers[topic.name] = consumer
            return consumer

    def assert_messages_keys(expected_dimensions, received_messages):
        ''' Assert that expected_dimensions are in received_messages JSON for
        each message in received_messages. You can check that one dimension is
        NOT included if expected_dimension[dim] is None'''

        assert(len(expected_dimensions) == len(received_messages))
        for dimensions, message in zip(expected_dimensions, received_messages):
            message = json.loads(message)
            print('Dimensions: {}'.format(dimensions))
            print('Message: {}'.format(message))
            for dimension, value in dimensions.items():
                if value is None:
                    assert(dimension not in message)
                else:
                    assert(message[dimension] == value)

    def check_kafka_messages(self,
                             check_messages_callback,
                             topic_name,
                             messages):
        ''' Check kafka messages '''
        try:
            topic_name = topic_name.encode()
        except AttributeError:
            pass  # Already in bytes

        consumer = self.__consumer(topic_name)
        consumed_messages = list(itertools.islice(consumer, 0, len(messages)))

        check_messages_callback(messages, [m.value for m in consumed_messages])

    def assert_all_messages_consumed(self):
        num_consumers = len(self.__kafka_consumers)
        if num_consumers == 0:
            return

        with ThreadPoolExecutor(max_workers=num_consumers) as pool:
            block = False
            consumer_futures = [pool.submit(consumer.consume, block)
                                for consumer
                                in self.__kafka_consumers.values()]

            # No messages should have been seen for any consumer for
            # consumer_timeout_ms
            return [None] * len(consumer_futures) == \
                   [f.result() for f in as_completed(consumer_futures)]
