/*
*
* CHAD assembler
*
*/

#include "chad_asm.h"

char *path_imem, *path_dmem;


bool immediate_to_int(char* in, unsigned int* out, label* labels, int labels_count) {
	int i, pos, len, label_len;
	if ((in[0] >= '0' && in[0] <= '9') || in[0]=='-' || (in[0]=='0'&&(in[1]=='x'||in[1]=='X'))) return char_to_unsigned_int(in, out);
	len = strlen(in);

	for (i=0; i<labels_count; i++) {
		label_len = strlen(labels[i].name);
		if (label_len != len) continue;
		for (pos=0; pos<label_len; pos++) {
			if (labels[i].name[pos] != in[pos]) pos=label_len+10;
		}
		if (pos==label_len) {
			printd(VERBOSITY_MAX, "identilabel %s\n", in);
			*out = labels[i].pointer;
			return 0;
		}
	}
	return 1;
}

char string_to_opcode(char* in) {
	int i;
	unsigned long in_hash = hash(in);
	for (i=0; i<=19; i++) {
		//printf("comparing '%s' to '%s' with result %d iff %llu==%llu\n", in, STR_OPCODES[i], in_hash==HASH_OPCODES[i], hash(in), HASH_OPCODES[i]);
		if (in_hash==HASH_OPCODES[i]) return (char)i;
	}
	return -1;	
}

char string_to_register(char* in) {
	int i, last;
	last=strlen(in)-1;
	if (in[last]==' ') pop_char(in, last);
	if (in[0]=='$') pop_char(in, 0);
	unsigned long in_hash = hash(in);
	for (i=0; i<=15; i++) {
		if (in_hash==HASH_REGISTERS[i]) return (char)i;
	}
	return -1;
}

bool is_label(char* line) {
	char* p = line;
	if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) return false;
	for (p=p+1; *p != 0 && (int)(p-line) < MAX_SIZE_LABEL; p++) {
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) {
			return *p == ':';
		}
	}
	
	return false;
}

bool is_dotword(char* line) {
	char* p = line;
	if (strlen(p) <= 5 || p[0]!='.' || p[1]!='w' || p[2]!='o' || p[3]!='r' || p[4]!='d' || p[5]!=' ') return false;
	return true;
}

void sanitize_line(char* line) {
	char* p_new, *p;
	int i, len, new_len;
	bool saw_space;
	
	len = strlen(line);
	p = line;
	p_new = malloc(len*sizeof(char));
	
	saw_space = true; // remove spaces in the beginning too
	new_len=0;
	for (; *p != 0; p++) {
		if (*p == CHAR_COMMENT || *p=='\n' || *p=='\r') {
			//printf("saw end at %c\n",*p);
			*p = 0;
			break;
		} else if (*p==' '||*p=='\t') {
			*p=' '; //replace tab with space
			if (!saw_space) {
				p_new[new_len]=' ';
				new_len++;
			}
			saw_space = true;
		} else if (*p==',') {
			continue;
		} else {
			saw_space = false;
			p_new[new_len]=*p;
			new_len++;
		}
	}
	
	for (i=0; i<new_len; i++) line[i]=p_new[i];
	line[new_len]=0;
	if (line[new_len-1]==' ') line[new_len-1]=0;
}

char* split_pop(char* line, char delimiter, int index) {
	char* out;
	char** lines;
	int i,j;
	if (index<0) return NULL;
	split(line, delimiter, &lines);
	for(i=0; i<index && lines[i]; i++);
	if (i==index) {
		out = malloc((strlen(lines[i])+1)*sizeof(char));
		for (j=0; j<strlen(lines[i]); j++) out[j]=lines[i][j];
		out[j]=0;
	} else out = NULL;
	free_lines(lines);
	return out;
}



unsigned long long instruction_to_unsigned_long_long(instruction ins) {
	unsigned long long number = 0;
	number += ((unsigned long long)ins.opcode)				<<40;
	number += ((unsigned long long)ins.rd)				<<36;
	number += ((unsigned long long)ins.rs)				<<32;
	number += ((unsigned long long)ins.rt)				<<28;
	number += 0								<<24; // reserved
	number += ((unsigned long long)(ins.immediate1&0x000000000FFF))	<<12;
	number += ((unsigned long long)(ins.immediate2&0x000000000FFF))	<<0;
	return number;
}

char* instruction_to_hex(instruction ins) {
	return unsigned_long_long_to_hex(instruction_to_unsigned_long_long(ins));
}

int compile(char** lines, char** lines_memory) {
	FILE *fptr;
	int i;
	int pc, len, labels_c, dotwords_c, address_int, value_int, len_lines;
	char *immediate1, *immediate2, *rt, *rd, *rs, *value, *address, *opcode;
	char* tmp;
	char* line;
	char** p;
	unsigned int* memory_initial = memtext_to_uint_arr(lines_memory);
	unsigned int dotwords[MAX_DMEM_ITEMS];
	instruction* instructions = (instruction*) malloc(MAX_SIZE_PC*sizeof(instruction));
	label* labels = (label*) malloc(MAX_SIZE_PC*sizeof(label));
	for (i=0; i<MAX_DMEM_ITEMS; i++) dotwords[i]=0;
	for (i=0; lines_memory[i]!=0; i++) dotwords[i]=memory_initial[i];

	for (p=lines; *p!=0; p++);
	len_lines = (int)(p - lines);
	if (len_lines > 1 && strlen(*(p-1)) == 0) len_lines--;


	labels_c = 0;
	dotwords_c = 0;
	pc = 0; // we only increase pc for regular instructions
	for (i=0; i<len_lines; i++) {
		//printf("loop at line %d, meaning %d<%d\n",i+1,i,len_lines);
		line = lines[i];
		len = strlen(line);
		if (!len) continue;
		sanitize_line(line);
		if (is_label(line)) {
			labels[labels_c].name = malloc((MAX_SIZE_LABEL+2)*sizeof(char));
			strcpy(labels[labels_c].name, line);
			labels[labels_c].name[strlen(line)-1] = 0;
			labels[labels_c].pointer = pc;
			labels_c++;
		} else if (is_dotword(line)) {
			if (count_occ(line, ' ') != 2) throw_error(ERROR_COMPILE_TIME, line);
			address = split_pop(line, ' ', 1);
			value = split_pop(line, ' ', 2);
			if (char_to_unsigned_int(address, &address_int)) throw_error(ERROR_COMPILE_TIME, line);
			if (char_to_unsigned_int(value, &value_int)) throw_error(ERROR_COMPILE_TIME, line);
			dotwords[address_int]=value_int;
			free(address);
			free(value);
			dotwords_c++;
		}
		else if (line[0] == 0) {
			continue;
		} else {
			pc++;
		}
	}
	//exit(1);
	pc = 0; // we only increase pc for regular instructions
	for (i=0; i<len_lines; i++) {
		line = lines[i];
		len = strlen(line);
		if (!len) continue;
		printd(VERBOSITY_HIGH, "Performing:\t'%s'\n", line);
		if (is_label(line)) {
			continue;
		} else if (is_dotword(line)) {
			continue;
		} else {
			if (count_occ(line, ' ') != 5) throw_error(ERROR_COMPILE_TIME, line);
			immediate2 = split_pop(line, ' ', 5);
			immediate1 = split_pop(line, ' ', 4);
			rt = split_pop(line, ' ', 3);
			rs = split_pop(line, ' ', 2);
			rd = split_pop(line, ' ', 1);
			opcode = split_pop(line, ' ', 0);
			instructions[pc].rt = string_to_register(rt);
			instructions[pc].rs = string_to_register(rs);
			instructions[pc].rd = string_to_register(rd);
			instructions[pc].opcode = string_to_opcode(opcode);
#if DEBUG==1
			instructions[pc].line = line;
#endif
			if (instructions[pc].rt==-1 || instructions[pc].rt==-1 ||
				instructions[pc].rs == -1 || instructions[pc].opcode == -1 ||
				immediate_to_int(immediate2, &instructions[pc].immediate2, labels, labels_c) ||
				immediate_to_int(immediate1, &instructions[pc].immediate1, labels, labels_c)) throw_error(ERROR_COMPILE_TIME, line);
			printd(VERBOSITY_MAX, "we haveaaa %d\n", instructions[pc].immediate2);
			free(immediate2);
			free(immediate1);
			free(rt);
			free(rd);
			free(rs);
			free(opcode);
			pc++;
		}
	}

	if ((fptr = fopen(path_dmem, "w"))==NULL) return ERROR_FILE_ACCESS;
	for (i = 0; i < MAX_DMEM_ITEMS; i++) {
		if (dotwords[i]==0) fprintf(fptr, "00000000\n");
		else {
			//printf("we are at %d with %d and %s\n", i, dotwords[i], unsigned_int_to_hex(dotwords[i]));
			tmp = unsigned_int_to_hex(dotwords[i]);
			fprintf(fptr, "%s\n", tmp+4); // we don't want the first 4
			free(tmp);
		}
	}
	fclose(fptr);

	if ((fptr = fopen(path_imem, "w"))==NULL) return ERROR_FILE_ACCESS;
	for (i = 0; i < pc; i++) {
		line = instruction_to_hex(instructions[i]);
		fprintf(fptr, "%s\n", line);
		free(line);
	}
	fclose(fptr);

	free(instructions);
	for (i=0; i<labels_c; i++) free(labels[i].name);
	free(labels);
	free(memory_initial);

	return COMPILED_SUCCESSFULLY;
}


int main(int argc, char *argv[]) {
	char** lines_program;
	char** lines_mem;
	int count_program, count_mem;

	int i=0;
	for(i=0;i<20;i++) HASH_OPCODES[i]=hash(STR_OPCODES[i]);
	for(i=0;i<16;i++) HASH_REGISTERS[i]=hash(STR_REGISTERS[i]);
	for(i=0;i<23;i++) HASH_IOREGISTERS[i]=hash(STR_IOREGISTERS[i]);

	if (argc < 2) {
		throw_error(ERROR_PARAMETERS, argv[0]);
		/*printf("Format is as following:\n");
		printf("%s <program path> <memory path>", argv[0]);
		return 1;*/
	}

	printf("Reading assembly file...\n");
	count_program = get_file_lines(argv[1], &lines_program);
	if (argc == 4) {
		path_imem = argv[2];
		path_dmem = argv[3];
	} else {
		path_imem = "imemin.txt";
		path_dmem = "dmemin.txt";
	}
	//if (argc==3) count_mem = get_file_lines(argv[2], &lines_mem);
	count_mem = split("", '\n', &lines_mem);

	printf("Compiling...\n");
	compile(lines_program, lines_mem);

	printf("Cleaning up...\n");
	free_lines(lines_program);
	free_lines(lines_mem);

	printf("Done. Goodbye!\n");
	return 0;
}
