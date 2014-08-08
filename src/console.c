#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "console.h"
#include "types.h"

#define MAX_LINE_WORDS 128

/* Global Variables */
HookRegisters *hook_head;
FetcherPacket packet;
extern ARMCPRegArray reg_array[14];

/* Command prototype */
static void cmd_display(int argc, char *argv[]);
static void cmd_undisplay(int argc, char *argv[]);
static void cmd_print(int argc, char *argv[]);
static void cmd_store(int argc, char *argv[]);
static void cmd_load(int argc, char *argv[]);
static void cmd_quit(int argc, char *argv[]);
static void cmd_help(int argc, char *argv[]);

/* Command array */
static CMDDefinition cmd[] = {
	{.name = "display", .handler = cmd_display, .desc = "Display a register. -> display $register_name[end_bit:start_bit]"},
	{.name = "undisplay", .handler = cmd_undisplay, .desc = "Undisplay a register. -> undisplay display_number"},
	{.name = "print", .handler = cmd_print, .desc = "Print a register value. -> print /x $register_name[end_bit:start_bit]"},
	{.name = "store", .handler = cmd_store, .desc = "Store display register list. -> store file_name"},
	{.name = "load", .handler = cmd_load, .desc = "Load command script. -> load file_name"},
	{.name = "quit", .handler = cmd_quit, .desc = "Terminate qemu-monitor."},
	{.name = "help", .handler = cmd_help, .desc = "Show this help guide."},
};

static void display_registers(FetcherPacket packet)
{
	HookRegisters *it;

	for(it = hook_head; it != NULL; it = it->next) {
		printf("%d: %s = ", it->id, it->name);
		switch(it->type) {
		case ARM_CP_UNIMPL:
			printf("UNIMPLEMENTED\n");
			break;
		case ARM_CP_CONST:
			printf("0x%lx\n", it->const_value);
			break;
		case ARM_CP_NORMAL_L:
			printf("0x%x\n",
			          (uint32_t)(*(uint32_t *)((uint8_t *)(&packet) + it->fieldoffset) & it->mask) >> it->start_bit);
			break;
		case ARM_CP_NORMAL_H:
			printf("0x%lx\n",
			          (*(uint64_t *)((uint8_t *)(&packet) + it->fieldoffset) & it->mask) >> it->start_bit);
			break;
		}
	}
}

static void desturctor()
{
	HookRegisters *it, *pre;

	/* Do not have hook register */
	if(hook_head == NULL) {
		return;
	}

	pre = hook_head;
	it = hook_head->next;
	while(pre) {
		free(pre);
		pre = it;
		if(it) {
			it = it->next;
		}
	}
}

static void parse_line(char *str)
{
	int count = 0;
	int i;
	char *pch;
	char *words[MAX_LINE_WORDS];

	for(pch = strtok(str, " \n"); pch != NULL; pch = strtok(NULL, " \n")) {
		strcpy(words[count] = malloc((strlen(pch) + 1) * sizeof(char)), pch);
		count++;
	}

	if(count == 0) { // empty string
		return;
	}

	/* Find command */
	for(i = 0; i < sizeof(cmd) / sizeof(CMDDefinition); i++) {
		if(!strcmp(cmd[i].name, words[0])) {
			break;
		}
	}

	if(i == sizeof(cmd) / sizeof(CMDDefinition)) { // cmd not found
		printf("Undefined command %s\n", words[0]);
	}
	else {
		cmd[i].handler(count - 1, (words + 1));
	}

	/* Free dynamic allocate memory for words */
	for(i = 0; i < count; i++) {
		free(words[i]);
	}
}

void _console_prompt()
{
	char input[128];

	while(1) {
		fgets(input, 128, stdin);
		parse_line(input);
		printf("-> ");
	}
}

void console_handle(FetcherPacket packet)
{
	printf("\n");
	display_registers(packet);
	printf("-> ");
}

/* Command handler implementation */
void cmd_display(int argc, char *argv[])
{
	HookRegisters *it = hook_head;
	int i, j;
	int valid = 0;
	int start_bit = 0, end_bit;
	uint64_t mask = 0xFFFFFFFFFFFFFFFF;
	char *pch;
	char reg_name[64];

	if(argc > 1) {
		printf("Too many arguments\n");
		return;
	}

	/* Parse register name.
	 * Register name format: leading dollar sign and optional bit field
	 * $reg_name, $reg_name[end_bit:start_bit]
	 */
	if(argv[0][0] != '$') {
		printf("Invalid register name\n");
		return;
	}
	if((pch = strchr(argv[0], '[')) != NULL) {
		sscanf(pch, "[%d:%d]", &end_bit, &start_bit);
		int len = end_bit - start_bit;

		mask >>= (63 - len);
		mask <<= start_bit;
		strncpy(reg_name, argv[0] + 1, pch - argv[0] - 1 < 64 ? pch - argv[0] - 1 : 64);
	}
	else {
		strncpy(reg_name, argv[0] + 1, 64);
	}

	/* Search register in registers array */
	for(i = 0; i < sizeof(reg_array) / sizeof(struct ARMCPRegArray); i++) {
		for(j = 0; j < reg_array[i].size; j++) {
			if(!strcmp(reg_array[i].array[j].name, reg_name)) {
				valid = 1;
				break;
			}
		}

		if(valid) {
			break;
		}
	}

	if(!valid) {
		printf("Invalid register name\n");
		return;
	}

	HookRegisters *tmp = (HookRegisters *)malloc(sizeof(HookRegisters));
	strncpy(tmp->name, argv[0] + 1, 64);
	tmp->next = NULL;
	tmp->mask = mask;
	tmp->const_value = reg_array[i].array[j].const_value;
	tmp->type = reg_array[i].array[j].type;
	tmp->fieldoffset = reg_array[i].array[j].fieldoffset;
	tmp->start_bit = start_bit;

	printf("Add register \"%s\" to hook list\n", argv[0]);

	/* First element */
	if(it == NULL) {
		tmp->id = 0;
		hook_head = tmp;
		return;
	}

	/* Iterate to last element */
	for(; it != NULL; it = it->next) {
		if(!strcmp(it->name, argv[0] + 1)) {
			printf("Register \"%s\" has already in hook list\n", argv[0] + 1);
			return;
		}

		if(it->next == NULL) {
			tmp->id = it->id + 1;
			it->next = tmp;
			break;
		}
	}
}

void cmd_undisplay(int argc, char *argv[])
{
	HookRegisters *it = hook_head, *prev;
	int id;

	if(argc > 1) {
		printf("Too many arguments\n");
	}

	id = atoi(argv[0]);

	if(it == NULL) {
		printf("Display list is empty!\n");
		return;
	}

	for(prev = NULL; it != NULL; prev = it, it = it->next) {
		if(it->id == id) {
			printf("Undisplay \"%s\"\n", it->name);
			if(prev == NULL) {
				hook_head = it->next;
				free(it);
			}
			else {
				prev->next = it->next;
				free(it);
			}
			return;
		}
	}
	printf("Display number %s is not in the hook list!\n", argv[0]);
}

void cmd_print(int argc, char *argv[])
{
	ARMCPRegInfo tmp;
	uint64_t value;
	int valid = 0, i, j;
	int start_bit = 0, end_bit;
	char str[128] = {0};
	char reg_name[64] = {0};
	char reg[64] = {0};
	char format = 'x'; // default format hexadecimal
	char *pch;
	uint64_t mask = 0xFFFFFFFFFFFFFFFF;

	/* Print register format:
	 * print $MIDR[31:16]
	 * print /x $MIDR[31:16] (x, d, u, o)
	 */
	if(argc == 1) {
		strncpy(reg, argv[0], 64);
	}
	else if(argc == 2) {
		if(argv[0][0] != '/') {
			printf("Invalid format\n");
			return;
		}
		format = argv[0][1];
		strncpy(reg, argv[1], 64);
	}
	else {
		printf("Too many arguments\n");
		return;
	}

	if(reg[0] != '$') {
		printf("Invalid register name\n");
		return;
	}
	
	pch = strchr(reg, '[');
	if(pch != NULL) {
		sscanf(pch, "[%d:%d]", &end_bit, &start_bit);
		strncpy(reg_name, reg + 1, pch - reg - 1 < 64 ? pch - reg - 1 : 64);
		int len = end_bit - start_bit;
		mask >>= (63 - len);
		mask <<= start_bit;
	}
	else {
		strncpy(reg_name, reg + 1, 64);
	}
		

	/* Search register in registers array */
	for(i = 0; i < sizeof(reg_array) / sizeof(struct ARMCPRegArray); i++) {
		for(j = 0; j < reg_array[i].size; j++) {
			if(!strcmp(reg_array[i].array[j].name, reg_name)) {
				valid = 1;
				tmp = reg_array[i].array[j];
				break;
			}
		}

		if(valid) {
			break;
		}
	}

	if(!valid) {
		printf("Invalid register name\n");
		return;
	}

	switch(tmp.type) {
	case ARM_CP_UNIMPL:
		printf("UNIMPLEMENTED\n");
		return;
	case ARM_CP_CONST:
		value = tmp.const_value;
		break;
	case ARM_CP_NORMAL_L:
		value = *(uint32_t *)((uint8_t *)(&packet) + tmp.fieldoffset);
		break;
	case ARM_CP_NORMAL_H:
		value = *(uint64_t *)((uint8_t *)(&packet) + tmp.fieldoffset);
		break;
	}

	switch(format) {
	case 'o':
		sprintf(str, "%s = %#lo\n", reg + 1, (value & mask) >> start_bit);
		break;
	case 'x':
		sprintf(str, "%s = %#lx\n", reg + 1, (value & mask) >> start_bit);
		break;
	case 'd':
		sprintf(str, "%s = %ld\n", reg + 1, (value & mask) >> start_bit);
		break;
	case 'u':
		sprintf(str, "%s = %lu\n", reg + 1, (value & mask) >> start_bit);
		break;
	default:
		sprintf(str, "Invalid format\n");
	}
	printf("%s", str);
}

void cmd_store(int argc, char *argv[])
{
	HookRegisters *it;
	char filename[32] = "cli.cmd"; // default output file name
	FILE *fout;

	if(argc > 1) {
		printf("Too many arguments\n");
		return;
	}

	if(argc == 1) {
		strncpy(filename, argv[0], 32);
	}

	// TODO: file process exception handling
	fout = fopen(filename, "w");

	for(it = hook_head; it != NULL; it = it->next) {
		fprintf(fout, "display $%s\n", it->name);
	}

	printf("Store display list to \"%s\"\n", filename);
	fclose(fout);
}

void cmd_load(int argc, char *argv[])
{
	char filename[32] = "cli.cmd"; // default input file name
	char line_buf[128] = {0};
	FILE *fin;

	if(argc > 1) {
		printf("Too many arguments\n");
		return;
	}

	if(argc == 1) {
		strncpy(filename, argv[0], 32);
	}

	// TODO: file process exception handling
	fin = fopen(filename, "r");
	printf("Load command script from \"%s\"\n", filename);

	while(fgets(line_buf, 128, fin) != NULL) {
		printf("-> %s", line_buf);
		parse_line(line_buf);
	}

	fclose(fin);
}

void cmd_quit(int argc, char *argv[])
{
	desturctor();
	pthread_exit(0);
}

void cmd_help(int argc, char *argv[])
{
	int i;

	for(i = 0; i < sizeof(cmd) / sizeof(CMDDefinition); i++) {
		printf("%s : %s\n", cmd[i].name, cmd[i].desc);
	}
}
