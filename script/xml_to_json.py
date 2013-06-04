#!/usr/bin/env python

import sys
import argparse
import json
from lxml import etree

from generate_code import Signal

def fatal_error(message):
    # TODO add red color
    sys.stderr.write("ERROR: %s\n" % message)
    sys.exit(1)


def warning(message):
    # TODO add yellow color
    sys.stderr.write("WARNING: %s\n" % message)


class Network(object):
    """Represents all the messages on a single bus."""

    def __init__(self, tree, all_messages):
        self.messages = {}

        for message_id, message in all_messages.items():
            node = tree.find("/Node/TxMessage[ID=\"%s\"]" % message_id)
            if node is None:
                warning("Unable to find message ID %d in XML" % message_id)
            else:
                self.messages[message_id] = Message(node, message['signals'])

    def to_dict(self):
        return {"messages": dict(("0x%x" % message.id,
                    message.to_dict())
                for message in list(self.messages.values())
                if len(message.signals) > 0)}


class Message(object):
    """Contains a single CAN message."""

    def __init__(self, node, mapped_signals):
        self.signals = []

        self.name = node.find("Name").text
        self.id = int(node.find("ID").text, 0)

        for signal_name in mapped_signals.keys():
            mapped_signal_node = node.find("Signal[Name=\"%s\"]" % signal_name)
            if mapped_signal_node is not None:
                mapped_signal = Signal.from_xml_node(mapped_signal_node)
                mapped_signal.generic_name = signal_name
                self.signals.append(mapped_signal)

    def to_dict(self):
        return {"name": self.name,
                "signals": dict((signal.name, signal.to_dict())
                    for signal in self.signals)}


def parse_options(argv):
    parser = argparse.ArgumentParser(
            description="Convert Canoe XML to the OpenXC JSON format.")
    parser.add_argument("database", help="Name of Canoe XML file")
    parser.add_argument("mapping_filename",
            help="Path to a JSON file with CAN messages mapped to OpenXC names")
    parser.add_argument("out", default="dump.json",
            help="Name out output JSON file")

    return parser.parse_args(argv)


def convert_to_json(database_filename, messages):
    if len(messages) == 0:
        warning("No messages specified for mapping from XML")
        return {}
    else:
        tree = etree.parse(database_filename)
        return Network(tree, messages).to_dict()


def main(argv=None):
    arguments = parse_options(argv)

    with open(arguments.mapping_filename, 'r') as mapping_file:
        mapping = json.load(mapping_file)
        data = convert_to_json(arguments.database, mapping.get('messages', []))

    with open(arguments.out, 'w') as output_file:
        json.dump(data, output_file, indent=4)
    print("Wrote results to %s" % arguments.out)


if __name__ == "__main__":
    sys.exit(main())
