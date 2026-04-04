#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <lua5.3/lua.h>
#include <lua5.3/lualib.h>
#include <lua5.3/lauxlib.h>

#define NRF24 1
#define EPF011 2

#if 0
#define DPS_SFR 0x92
#else
#define DPS_SFR 0x86
#endif

#include "core.h"

static pthread_mutex_t uart_mutex = PTHREAD_MUTEX_INITIALIZER;
static int uart_fd = 0;

static lua_State *L;

FILE *serial;

static void die(const char *msg, ...)
{
	va_list ap;
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

static void die_errno(const char *msg, ...)
{
	va_list ap;
	char *s;
	size_t size;
	FILE *f = open_memstream(&s, &size);
	va_start(ap, msg);
	vfprintf(f, msg, ap);
	va_end(ap);
	fclose(f);
	die("%s: %s", s, strerror(errno));
	free(s);
}

void opcode_a5()
{
	int request = peek_sfr(0xff);
	fprintf(stderr, "Opcode A5 (sfr 0xff=%02x)\n", request);
	switch (request) {
	case 41: {
		fprintf(stderr, "!41! => ACC=%02x R0=%02x\n", peek_sfr(SFR_ACC), peek_sfr(0));
		break;
		}
	case 42: {
		int buf = peek_sfr(1);
		int cnt = peek_sfr(SFR_ACC);
		fprintf(stderr, "!42! => A=%02x R1=%02x: rx gdb cmd: ", cnt, buf);
		for (int i = 0; i < cnt; i++) {
			char c = peek_iram(buf + i);
			if (c >= 32 && c < 128) {
				fputc(c, stderr);
			} else {
				fprintf(stderr, "%%%02x\n", (uint8_t)c);
			}
		}
		fprintf(stderr, "\n");
		break;
		}
	default:
		fprintf(stderr, "Unhandled emulator request: %d\n", request);
		break;
	}
}

const char *sfr_name(uint8_t addr) {
	switch (addr) {
	case DPS_SFR: return "DPS";
	case SFR_SCON: return "SCON";
	case SFR_SBUF: return "SBUF";
	default:   return NULL;
	}
}

static int ramd_hook(int write, uint8_t addr, uint8_t *data, const char **sfr_name) {
	int top = lua_gettop(L);
	int ret = lua_getglobal(L, "ramd_hooks");
	int retval = 1;
	if (ret != LUA_TTABLE) {
		retval = 0;
		goto exit_lua_stackcheck;
	}
	lua_pushinteger(L, addr);
	ret = lua_gettable(L, -2);
	if (ret != LUA_TFUNCTION) {
		lua_pop(L, 2);
		retval = 0;
		goto exit_lua_stackcheck;
	}
	// remove table from stack
	lua_remove(L, -2);
	lua_pushboolean(L, write);
	lua_pushinteger(L, *data);
	// call with 2 args and 2 return values
	lua_call(L, 2, 2);
	*sfr_name = lua_tostring(L, -2);
	*data = lua_tointeger(L, -1);
	lua_pop(L, 2);
exit_lua_stackcheck:
	if (top != lua_gettop(L)) {
		fprintf(stderr, "unbalanced: %d -> %d\n", top, lua_gettop(L));
	}
	return retval;
}

int writex_hook(uint16_t addr, uint8_t val)
{
}

uint8_t ramd_get(uint8_t *ram, uint8_t addr) {
	uint8_t ret = 0xff;
	const char *sfr = sfr_name(addr);

	switch (addr) {
	case SFR_SCON: /* SCON */
		pthread_mutex_lock(&uart_mutex);
		ret = ram[SFR_SCON];
		pthread_mutex_unlock(&uart_mutex);
		break;
	case SFR_SBUF: /* SBUF */
		pthread_mutex_lock(&uart_mutex);
		ret = ram[SFR_SBUF];
		pthread_mutex_unlock(&uart_mutex);

		printf("SBUF=%02x (%c)\n", ret, ret > 0x20 && ret < 0x80 ? ret : '?');
		return ret;
	default:
		if (!ramd_hook(0, addr, &ram[addr], &sfr)) {
			printf("Unsupported SFR@%02x\n", addr);
		}
		ret = ram[addr];
	}
	if (get_trace()) {
		if (sfr) {
			printf("ramd_get(%02x [%s]) => %02x @%04x\n", addr, sfr, ret, get_pc());
		} else {
			printf("ramd_get(%02x) => %02x @%04x\n", addr, ret, get_pc());
		}
	}
	return ret;
}

void ramd_put(uint8_t *ram, uint8_t addr, uint8_t val) {
	const char *sfr = sfr_name(addr);
	switch (addr) {
	case DPS_SFR: /* DPS */
		ram[DPS_SFR] = val & 1;
		if (val & 1) {  /* DPTR = DPH1+DPL1*/
			map_dptr(&ram[0x84], &ram[0x85]);
			printf("DPTR1 => %04x\n", get_dptr());
		} else {  /* DPTR = DPH+DPL */
			map_dptr(&ram[0x82], &ram[0x83]);
			printf("DPTR => %04x\n", get_dptr());
		}
		break;
	case SFR_SCON: /* SCON */
		pthread_mutex_lock(&uart_mutex);
		ram[SFR_SCON] = val;
		pthread_mutex_unlock(&uart_mutex);
		break;
	case SFR_SBUF: /* SBUF */
		fputc(val, serial);
		fflush(serial);
		printf("SBUF=%02x (%c)\n", val, val > 0x20 && val < 0x80 ? val : '?');
		write(uart_fd, &val, 1);

		pthread_mutex_lock(&uart_mutex);
		ram[SFR_SBUF] = val;
		pthread_mutex_unlock(&uart_mutex);
		poke_sfr(SFR_SCON, ram[SFR_SCON] | SCON_TI);
		break;
	default:
		ram[addr] = val;
		if (!ramd_hook(1, addr, &ram[addr], &sfr)) {
			printf("Unsupported SFR@%02x\n", addr);
		}
		break;
	}
	if (get_trace()) {
		if (sfr) {
			printf("ramd_put(%02x [%s], %02x) @%04x\n", addr, sfr, val, get_pc());
		} else {
			printf("ramd_put(%02x, %02x) @%04x\n", addr, val, get_pc());
		}
	}
}

static void sfr_init(void) {
	memset(ramsfrtrap, 0xff, sizeof(ramsfrtrap));
	memset(ramsfrtrap, 0x00, sizeof(ramsfrtrap) / 2);
	SFRNOTRAP(0x81);  // SP
	SFRNOTRAP(0x82);  // DPL
	SFRNOTRAP(0x83);  // DPH
	SFRNOTRAP(0xd0);  // PSW
	SFRNOTRAP(0xe0);  // ACC
	SFRNOTRAP(0xf0);  // B
	poke_sfr(0x81, 0x07); // SP = 0x07
}

struct buffer {
	const char *name;
	char *ptr;
	int size;
	struct buffer *next;
};

struct buffer *buffer_head = NULL;

static void create_buffer(const char *name, int size)
{
	struct buffer *b = calloc(sizeof(*b) + size, 1);
	b->name = strdup(name);
	b->ptr = (void*)(b + 1);
	b->size = size;
	b->next = buffer_head;
	buffer_head = b;
}

static char *get_buffer(const char *name)
{
	struct buffer *b = buffer_head;
	while (b != NULL) {
		if (strcmp(b->name, name) == 0) {
			return b->ptr;
		}
		b = b->next;
	}
	fprintf(stderr, "buffer not found: %s\n", name);
	return NULL;
}

static int l_hello(lua_State *L) {
	fprintf(stderr, "Hello world!\n");
	return 0;
}

static int l_load_rom(lua_State *L) {
	int nargs = lua_gettop(L);
	if (nargs != 2) {
		lua_pushliteral(L, "load_rom: not enough arguments");
		lua_error(L);
		return 0;
	}
	if (!lua_isstring(L, 1)) {
		lua_pushliteral(L, "arg1 want string");
		lua_error(L);
		return 0;
	}
	if (!lua_isnumber(L, 2)) {
		lua_pushliteral(L, "arg2 want number");
		lua_error(L);
		return 0;
	}
	const char *name = lua_tostring(L, 1);
	int size = lua_tointeger(L, 2);
	FILE *f = fopen(name, "rb");
	if (f == NULL) {
		lua_pushstring(L, strerror(errno));
		lua_error(L);
		return 0;
	}
	printf("create_buffer(%s, %d)\n", name, size);
	create_buffer(name, size);

	char *buf = get_buffer(name);
	size_t nr = fread(buf, 1, size, f);
	fclose(f);
	if (nr != size) {
		lua_pushliteral(L, "file is too short!");
		lua_error(L);
	}
	return 0;
}

static int l_make_ram(lua_State *L) {
	int nargs = lua_gettop(L);
	if (nargs != 2) {
		lua_pushliteral(L, "make_ram: not enough arguments");
		lua_error(L);
		return 0;
	}
	if (!lua_isstring(L, 1)) {
		lua_pushliteral(L, "arg1 want string");
		lua_error(L);
		return 0;
	}
	if (!lua_isnumber(L, 2)) {
		lua_pushliteral(L, "arg2 want number");
		lua_error(L);
		return 0;
	}
	const char *name = lua_tostring(L, 1);
	int size = lua_tointeger(L, 2);
	printf("create_buffer(%s, %d)\n", name, size);
	create_buffer(name, size);
	return 0;
}

static int l_map_rom(lua_State *L) {
	int nargs = lua_gettop(L);
	if (nargs != 4) {
		lua_pushliteral(L, "nargs != 4");
		lua_error(L);
		return 0;
	}
	if (!lua_isstring(L, 1)) {
		lua_pushliteral(L, "arg1 want string");
		lua_error(L);
		return 0;
	}
	char *buf = get_buffer(lua_tostring(L, 1));
	if (buf == NULL) {
		lua_pushliteral(L, "named buffer not found");
		lua_error(L);
		return 0;
	}

	if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) {
		lua_pushliteral(L, "arg2+ want number");
		lua_error(L);
		return 0;
	}
	int start = lua_tointeger(L, 2);
	int len = lua_tointeger(L, 3);
	int ofs = lua_tointeger(L, 4);

	map_rom(buf, start, len, ofs);
	return 0;
}

static int l_map_xram(lua_State *L) {
	int nargs = lua_gettop(L);
	if (nargs != 5) {
		lua_pushliteral(L, "map_xram: nargs != 5");
		lua_error(L);
		return 0;
	}
	if (!lua_isstring(L, 1)) {
		lua_pushliteral(L, "arg1 want string");
		lua_error(L);
		return 0;
	}
	char *buf = get_buffer(lua_tostring(L, 1));
	if (buf == NULL) {
		lua_pushliteral(L, "named buffer not found");
		lua_error(L);
		return 0;
	}

	if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) {
		lua_pushliteral(L, "arg2+ want number");
		lua_error(L);
		return 0;
	}
	int n1 = lua_tointeger(L, 2);
	int n2 = lua_tointeger(L, 3);
	int ofs = lua_tointeger(L, 4);
	int readonly = lua_tointeger(L, 5);

	if (readonly) {
		map_xram_ro(buf, n1, n2, ofs);
	} else {
		map_xram(buf, n1, n2, ofs);
	}
	return 0;
}

static int l_map_irq(lua_State *L) {
	lua_pushliteral(L, "not implemented");
	lua_error(L);
	return 0;
}

static int l_peek_sfr(lua_State *L) {
	int nargs = lua_gettop(L);
	if (nargs != 1) {
		lua_pushliteral(L, "nargs != 1");
		lua_error(L);
		return 0;
	}
	if (!lua_isnumber(L, 1)) {
		lua_pushliteral(L, "arg1 want number");
		lua_error(L);
		return 0;
	}
	int addr = lua_tointeger(L, 1);
	lua_pushinteger(L, peek_sfr(addr));
	return 1;
}

static int l_peek_xram(lua_State *L) {
	int nargs = lua_gettop(L);
	if (nargs != 1) {
		lua_pushliteral(L, "nargs != 1");
		lua_error(L);
		return 0;
	}
	if (!lua_isnumber(L, 1)) {
		lua_pushliteral(L, "arg1 want number");
		lua_error(L);
		return 0;
	}
	int addr = lua_tointeger(L, 1);
	lua_pushinteger(L, peek_xram(addr));
	return 1;
}

static int l_poke_xram(lua_State *L) {
	int nargs = lua_gettop(L);
	if (nargs != 2) {
		lua_pushliteral(L, "nargs != 2");
		lua_error(L);
		return 0;
	}
	if (!lua_isnumber(L, 1)) {
		lua_pushliteral(L, "arg1 want number");
		lua_error(L);
		return 0;
	}
	if (!lua_isnumber(L, 2)) {
		lua_pushliteral(L, "arg2 want number");
		lua_error(L);
		return 0;
	}
	int addr = lua_tointeger(L, 1);
	int v = lua_tointeger(L, 2);
	poke_xram(addr, v);
	return 0;
}

static int l_poke_sfr(lua_State *L) {
	int nargs = lua_gettop(L);
	if (nargs != 2) {
		lua_pushliteral(L, "nargs != 2");
		lua_error(L);
		return 0;
	}
	if (!lua_isnumber(L, 1)) {
		lua_pushliteral(L, "arg1 want number");
		lua_error(L);
		return 0;
	}
	if (!lua_isnumber(L, 2)) {
		lua_pushliteral(L, "arg2 want number");
		lua_error(L);
		return 0;
	}
	int addr = lua_tointeger(L, 1);
	int v = lua_tointeger(L, 2);
	poke_sfr(addr, v);
	return 0;
}

static int l_peek_rom(lua_State *L) {
	int nargs = lua_gettop(L);
	if (nargs != 1) {
		lua_pushliteral(L, "nargs != 1");
		lua_error(L);
		return 0;
	}
	if (!lua_isnumber(L, 1)) {
		lua_pushliteral(L, "arg1 want number");
		lua_error(L);
		return 0;
	}
	int addr = lua_tointeger(L, 1);
	char *rom = get_buffer("rom");
	lua_pushinteger(L, (uint8_t)rom[addr]);
	return 1;
}

static int l_poke_pcx(lua_State *L) {
	int nargs = lua_gettop(L);
	if (nargs != 1) {
		lua_pushliteral(L, "poke_pcx: nargs != 1");
		lua_error(L);
		return 0;
	}
	if (!lua_isnumber(L, 1)) {
		lua_pushliteral(L, "poke_pcx: arg1 want number");
		lua_error(L);
		return 0;
	}
	int v = lua_tointeger(L, 1);
	poke_pcx(v);
	return 0;
}

static const luaL_Reg emu_lib[] = {
	{"hello",   l_hello},
	{"make_ram", l_make_ram},
	{"load_rom", l_load_rom},
	{"map_rom", l_map_rom},
	{"map_xram", l_map_xram},
	{"map_irq", l_map_irq},
	{"peek_xram", l_peek_xram},
	{"peek_sfr", l_peek_sfr},
	{"peek_rom", l_peek_rom},
	{"poke_xram", l_poke_xram},
	{"poke_sfr", l_poke_sfr},
	{"poke_pcx", l_poke_pcx},
	{NULL, NULL}
};

static int lua_init(const char *initscript) {
	int err;
	L = luaL_newstate();
	luaL_openlibs(L);
	luaL_newlib(L, emu_lib);
	lua_setglobal(L, "emu");
	err = luaL_dofile(L, initscript);
	if (err != LUA_OK) {
		fprintf(stderr, "luaL_loadfile %s: %d (%s)\n", initscript, err, lua_tostring(L, -1));
		return err;
	}
	//luaL_dostring(L, "test()");
	return LUA_OK;
}

static const char *chip = "";
static const char *serout = "/tmp/serial.txt";

static void parseopts(int *argc, char ***argv)
{
	int res;
	while ((res = getopt(*argc, *argv, "c:s:")) != -1) {
		switch (res) {
		default:
			fprintf(stderr, "bad option\n");
			exit(1);
		case 'c':
			chip = optarg;
			break;
		case 's':
			serout = optarg;
			break;
		}
	}
	*argc -= optind;
	*argv += optind;
}

static void *uart_listener_thread(void *priv)
{
	uint16_t *uart_port = priv;
	int sock = socket(PF_INET, SOCK_STREAM, 0);
	struct sockaddr_in sa = {
	.sin_family = AF_INET,
	.sin_port = htons(*uart_port),
	.sin_addr = 0,
	};
	int i = 1;
	printf("Listening for uart connection on 0.0.0.0:%d\n", *uart_port);
	if (sock == -1) {
		die_errno("socket");
	}
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) == -1) {
		die_errno("setsockopt");
	}
	if (bind(sock, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
		die_errno("bind");
	}
	if (listen(sock, 1)) {
		die_errno("listen");
	}
	while (1) {
		struct sockaddr_in remote = {0};
		socklen_t salen = sizeof(remote);
		int conn = accept(sock, (struct sockaddr*)&remote, &salen);
		if (conn == -1) {
			die_errno("accept");
		}
		pthread_mutex_lock(&uart_mutex);
		uart_fd = conn;
		pthread_mutex_unlock(&uart_mutex);
		while (1) {
			uint8_t d;
			ssize_t len = recv(conn, &d, 1, 0);
			if (!len) {
				/* connection closed */
				fprintf(stderr, "uart connection closed!\n");
				fflush(stderr);
				close(conn);
				break;
			}
			fputc(d, serial);
			fflush(serial);
			fputc(d, stderr);


			/* Access SFR ram directly to avoid deadlock on the mutex */
			uint8_t *sfr_scon = sfr_ptr(SFR_SCON);
			uint8_t *sfr_sbuf = sfr_ptr(SFR_SBUF);
			while (1) {
				bool ok = false;
				pthread_mutex_lock(&uart_mutex);
				if ((*sfr_scon & SCON_RI) == 0) {
					*sfr_sbuf = d;
					*sfr_scon |= SCON_RI;
					ok = true;
				}
				pthread_mutex_unlock(&uart_mutex);
				if (ok) break;
				usleep(100000);
			}
			usleep(100000);
		}
	}
	return NULL;
}

void handle_breakpoint(uint32_t full_pc) {
	printf("bp at 0x%08x\n", full_pc);
	if (full_pc != 0x261f5) {
		//set_trace(false, 0);
		set_breakpoint(0xffffff, 0x261f5);
		return;
	}
	uint8_t sp = peek_sfr(SFR_SP);
	uint8_t addrh = peek_iram(sp);
	uint8_t addrl = peek_iram(sp-1);
	uint16_t addr = addrl + (addrh << 8);
	uint8_t parm2l = peek_iram(0x79);
	uint8_t parm2h = peek_iram(0x7a);
	uint16_t parm2 = parm2l + (parm2h << 8);
	uint8_t parm1 = peek_sfr(SFR_DPL);
	printf("ret addr: 0x%04x (sp=0x%02x) start=%d cmd=0x%04x\n", addr, sp, parm1, parm2);
	set_trace(true, 0);
	set_breakpoint(0xffff, addr);
}

pthread_t uart_listener;

int main(int argc, char **argv)
{
	FILE *f;
	struct stat sb;
	uint16_t uart_port = 1212;
	parseopts(&argc, &argv);

	if (strcmp(chip, "") == 0) {
		die("Set chip with -c");
	}

	if (argc < 1) {
		die("bin file arg is mandatory");
	}

	f = fopen(argv[0], "r");
	if (fstat(fileno(f), &sb) != 0) {
		die("fstat");
	}
	int paddedsize = (sb.st_size + 255) & ~255;
	printf("ROM size: %d (%d)\n", sb.st_size, paddedsize);
	create_buffer("rom", paddedsize);
	char *rom = get_buffer("rom");

	static struct sfr_bit uart0_irqenable = { NULL, 0x10, 0 };
	static struct sfr_bit uart0_irqsource = { NULL, 0x03, 0 };
	static struct sfr_bit uart0_irqprio   = { NULL, 0x10, 0 };
	uart0_irqenable.data = sfr_ptr(SFR_IE);
	uart0_irqsource.data = sfr_ptr(SFR_SCON);
	uart0_irqprio.data = sfr_ptr(SFR_IP);
	map_irq(4, 0x23, &uart0_irqenable, &uart0_irqsource, &uart0_irqprio);

	static struct sfr_bit timer2_irqenable = { NULL, 0x20, 0 };
	static struct sfr_bit timer2_irqsource = { NULL, 0x80, 0 };
	static struct sfr_bit timer2_irqprio   = { NULL, 0x20, 0 };
	timer2_irqenable.data = sfr_ptr(SFR_IE);
	timer2_irqsource.data = sfr_ptr(SFR_T2CON);
	timer2_irqprio.data = sfr_ptr(SFR_IP);
	map_irq(5, 0x2b, &timer2_irqenable, &timer2_irqsource, &timer2_irqprio);

	serial = fopen(serout, "a");
	fprintf(serial, "\n==== Emulator start! ====\n");

	fread(rom, 1, sb.st_size, f);

	pthread_create(&uart_listener, NULL, uart_listener_thread, &uart_port);

	sfr_init();
	char *initscript = malloc(strlen(chip) + 4);
	sprintf(initscript, "%s.lua", chip);
	if (lua_init(initscript) != LUA_OK) {
		return 1;
	}

/*
	set_trace(false, 0);
	set_breakpoint(0xffffff, 0x261f5);
*/

	run_8051();
	fclose(f);
	return 0;
}
