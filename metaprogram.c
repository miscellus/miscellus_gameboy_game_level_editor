#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


typedef struct Span {
	uint32_t offset;
	uint32_t length;
} Span;

typedef enum Token_Kind {
	TOKEN_KIND_NONE = 0,
	TOKEN_KIND_INT_LITERAL = 256,
} Token_Kind;

typedef struct Token {
	Token_Kind kind;
	Span span;
	union {
		int x;
	} u;
} Token;

typedef enum Scanner_State {
	SCANNER_STATE_ROOT = 0,
	SCANNER_STATE_STRUCT = 1,
} Scanner_State;

typedef struct Scanner {
	Scanner_State state;
	char *data;
	char *at;
	char *end;
} Scanner;

static void eat_whitespace(Scanner *scanner) {
	while (*scanner->at <= ' ') (*scanner->at)++;
}

static bool expect_and_eat(Scanner *scanner, char *literal_string) {
	char *compare_at = literal_string;

	while (*compare_at) {
		if (*compare_at != *scanner->at) {
			return false;
		}

		++compare_at;
		++scanner->at;
	}

	return true;
}

static inline bool is_decimal_char(char c) {
	return (c >= '0' && c <= '9');
}

static inline bool is_identifier_char_first(char c) {
	return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static inline bool is_identifier_char(char c) {
	return is_identifier_char_first(c) || is_decimal_char(c);
}

static long get_identifier(char *at, long buffer_length, char *buffer) {

	long length = 0;

	char c = at[length];

	if (is_identifier_char_first(c)) {
		do {
			buffer[length] = c;
			++length;
			c = at[length];
		} while (is_identifier_char(c));
	}
	
	assert(length < buffer_length);

	return length;
}

static long get_integer(char *at, long buffer_length, char *buffer) {
	long length = 0;

	char c = at[0];

	while (is_decimal_char(c)) {
		buffer[length] = c;
		++length;
		c = at[length];
	}
	
	assert(length < buffer_length);

	return length;
}

typedef enum Parse_State {
	Parse_State_STOP = 0,
	Parse_State_ROOT = 1,
	Parse_State_STRUCT = 2,
} Parse_State;

static void read_entire_file(char *path, uint64_t *out_length, uint8_t **out_contents) {

	FILE *file = fopen(path, "rb");
	if (!file) return;

	uint64_t length;
	uint8_t *contents;

	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	
	contents = malloc(length+1);

	uint64_t amount_read = fread(contents, 1, length, file);
	fclose(file);

	if (amount_read != length) return;

	contents[length] = '\0';

	*out_length = length;
	*out_contents = contents;
	return;
}

bool scanner_next_token(Scanner *scanner, Token *out_token) {
	if (scanner->index >= scanner->file_length) {
		return false;
	}

	switch(scanner->state) {
		case SCANNER_STATE_ROOT: {
			eat_whitespace(scanner);
		}
		break;
	}
	++scanner->index;

	return true;
}

int main(int argc, char **argv) {	
	if (argc <= 1) {
		return -1;
	}
	
	Scanner scanner = {0};
	read_entire_file(argv[1], &scanner.file_length, &scanner.file_contents);
	assert(scanner.file_contents);

	//printf("%*s\n", (int)file_length, file_contents);

	Token token;
	while (scanner_next_token(&scanner, &token)) {
		printf("scan\n");
	}

	return 0;
}
