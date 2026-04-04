#!/usr/bin/env python3

def gen(op_base, name, alu_op):
	print("""
{name}_ai,{op_base0},2
{NAME} A, #data
	ACC = {alu_op}(ACC, fetch(1));

{name}_ad,{op_base1},2
{NAME} A, addr
	ACC = {alu_op}(ACC, readd(fetch(1)));

{name}_r0,{op_base2},1
{NAME} A, @R0
	ACC = {alu_op}(ACC, readd(R0));

{name}_r1,{op_base3},1
{NAME} A, @R1
	ACC = {alu_op}(ACC, readd(R1));

{name}_ar,{op_baser},1
{NAME} A, Rx
	ACC = {alu_op}(ACC, RX);

""".format(
	op_base0=op_base + 0,
	op_base1=op_base + 1,
	op_base2=op_base + 2,
	op_base3=op_base + 3,
	op_baser=':'.join(['%d' % i for i in range(op_base + 4, op_base + 12)]),
	name=name,
	NAME=name.upper(),
	alu_op=alu_op))

def gen2(op_base, name, alu_op):
	print("""
{name}_ia,{op_base0},2
{NAME} iram, A
	tmp1 = fetch(1);
	writed(tmp1, {alu_op}(readd(tmp1), ACC));

{name}_id,{op_base1},3
{NAME} iram, #data
	tmp1 = fetch(1);
	writed(tmp1, {alu_op}(readd(tmp1), fetch(2)));

""".format(
	op_base0=op_base + 0,
	op_base1=op_base + 1,
	name=name,
	NAME=name.upper(),
	alu_op=alu_op))

gen(0x24, "add",  "alu_add")
gen(0x34, "addc", "alu_addc")
gen2(0x42, "orl", "alu_orl")
gen(0x44, "orl",  "alu_orl")
gen2(0x52, "anl", "alu_anl")
gen(0x54, "anl",  "alu_anl")
gen2(0x62, "xrl", "alu_xrl")
gen(0x64, "xrl",  "alu_xrl")
gen(0x94, "subb", "alu_subb")
