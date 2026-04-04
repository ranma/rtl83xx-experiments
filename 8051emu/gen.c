#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>

struct op {
	const char *name;
	const char *code;
	const char *desc;
	int bytes;
	int emitted;
};

struct op *opmap[256];

static void addop(int op, const char *name, int bytes, const char *code, const char *desc)
{
	struct op *new = malloc(sizeof(*new));
	new->name = name;
	new->bytes = bytes;
	new->code = code;
	new->desc = desc;
	new->emitted = 0;
	opmap[op] = new;
}

int main(int argc, char **argv)
{
	int i, j;
	FILE *f = fopen("allops.txt", "r");
	char buf[80];
	char *s;
	char *op_name = NULL;
	char *op_code = NULL;
	char *op_desc = NULL;
	char *op_idx = NULL;
	int op_bytes = 0;

	do {
		s = fgets(buf, sizeof(buf), f);
		if (s == NULL || strlen(s) == 0 || (strlen(s) == 1 && s[0] == '\n')) {
			int last_idx = -1;
			while (1) {
				char *t = strtok(op_idx, ":");
				op_idx = NULL;
				if (!t)
					break;
				int idx = atoi(t);
				if (last_idx == -1) {
					addop(idx, op_name, op_bytes, op_code, op_desc);
				} else {
					opmap[idx] = opmap[last_idx];
				}
				last_idx = idx;
			}
			op_idx = op_name = op_code = op_desc = NULL;
			op_bytes = 0;
		} else if (!op_name) {
			char *t;
			op_name = strdup(strtok(s, ","));
			if (t = strtok(NULL, ",")) {
				op_idx = strdup(t);
			}
			if (t = strtok(NULL, ",")) {
				op_bytes = atoi(t);
			}
		} else if (s[0] == '\t') {
			if (!op_code) {
				op_code = strdup(s);
			} else {
				char *new = malloc(strlen(op_code) + strlen(s) + 1);
				memcpy(new, op_code, strlen(op_code));
				op_code = strcat(new, s);
			}
		} else {
			op_desc = strdup(s);
			if (strlen(s) > 1 && op_desc[strlen(s) - 1] == '\n') {
				op_desc[strlen(s) - 1] = 0;
			}
		}
	} while (s != NULL);

	printf("\tstatic void *insnmap[256] = {\n");
	for (i = 0; i < 256; i += 8) {
		printf("\t\t/* %02x */", i);
		for (j = 0; j < 8; j++) {
			const struct op *op = opmap[i + j];
			printf(" &&%s,", op ? op->name : "unimpl");
		}
		printf("\n");
	}
	printf("\t};\n");

	printf("\tFETCH;\n");
	for (i = 0; i < 256; i++) {
		struct op *op = opmap[i];
		if (!op || op->emitted)
			continue;
		printf("%s:", op->name);
		if (op->desc) {
			printf(" /* %s */", op->desc);
		}
		printf("\n");
		if (op->code) {
			printf("%s", op->code);
		}
		if (op->bytes) {
			printf("\tpc += %d;\n", op->bytes);
		}
		printf("\tFETCH;\n");

		op->emitted = 1;
	}

	return 0;
}
