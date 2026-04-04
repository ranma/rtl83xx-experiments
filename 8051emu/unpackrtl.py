#!/usr/bin/env python3
"""Unpack RTL83xx image into 64KiB banks for emu51."""

import argparse

parser = argparse.ArgumentParser(prog='unpack')
parser.add_argument('filename')
parser.add_argument('-o', '--output', required=True)

args = parser.parse_args()
with open(args.filename, 'rb') as f:
  data = f.read()
l = len(data)

with open(args.output, 'wb') as f:
  f.write(data[2:0x4000])
  f.write(bytes(0xc002))
  data = data[0x4000:]
  while data:
    f.write(bytes(0x4000))
    f.write(data[0:0xc000])
    data = data[0xc000:]
