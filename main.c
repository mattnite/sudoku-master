// Sudoku Master Program
//
// Author: Matthew Knight
// File Name: main.c
// Date: 2020-01-19
//
// This program links to shared objects with a predefined ABI, and tests them
// against a set of sudoku puzzles. The timing statistics are written to stdout
// in csv format.

#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define SUDOKU_SIZE 81
#define SUDOKU_AXIS_SIZE 9

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

typedef int (*iterator_t)(int,int);

struct module {
    void* handle;
    const char* name;
    const char* author;
    int (*solve)(int*);
};

struct result {
    struct module module;
    size_t successes;
    size_t incorrect;
    uint64_t* data;
};

struct list_node {
    struct list_node* next;
    int puzzle[SUDOKU_SIZE];
};

int module_init(struct module* module, const char* filename);
int module_get_symbol(void* handle, void** dst, const char* sym);

int check(const int* puzzle);
int cross_check(const int* puzzle, int* solution);
int test(const struct module* module, int* puzzle, uint64_t* duration);

void list_append(struct list_node** list, struct list_node* node);
size_t list_size(const struct list_node* list);

int check_iterator(const int* puzzle, int (*iterator)(int, int), int i);
int row_iterator(int row, int col);
int col_iterator(int col, int row);
int cell_iterator(int cell, int pos);

int insert(uint64_t *arr, size_t len, size_t max, uint64_t val);
uint64_t timespec_to_us(struct timespec* val);

char peek(FILE* stream) {
	char ret = getc(stream);
	return ungetc(ret, stream);
}

int main(int argc, char* argv[])
{
	int status, i, j;
	size_t list_len;
	struct list_node *list = NULL, *node;
	struct result* results;

	while (!feof(stdin) && peek(stdin) != EOF) {
		const char *error;
		char buf[SUDOKU_AXIS_SIZE][SUDOKU_AXIS_SIZE + 2];
		struct list_node* node;

		if ((argc - 1) < 1) {
			fputs("no modules\n", stderr);
			return -1;
		}

		for (i = 0; i < ARRAY_SIZE(buf); ++i) {
			if (fgets(buf[i], SUDOKU_AXIS_SIZE + 2, stdin) == NULL) {
				if (feof(stdin)) {
					printf("got eof, i: %d\n", i);
				}

				if (ferror(stdin)) {
					fputs("error with file\n", stderr);
					return -1;
				}
			}

			if (strlen(buf[i]) != SUDOKU_AXIS_SIZE + 1) {
				fputs("error parsing line\n", stderr);
				return -1;
			}
		}

		node = malloc(sizeof(struct list_node));
		if (!node)
			return -ENOMEM;

		for (i = 0; i < SUDOKU_AXIS_SIZE; ++i)
			for (j = 0; j < SUDOKU_AXIS_SIZE; ++j)
				node->puzzle[row_iterator(i, j)]
					= (int)buf[i][j] - 0x30;

		status = check(node->puzzle);
		if (status < 0) {
			fputs("invalid puzzle\n", stderr);
			return status;
		}

		list_append(&list, node);
	}

	list_len = list_size(list);
	if (list_len == 0) {
		fputs("no puzzles\n", stderr);
		return -1;
	}

	results = calloc(sizeof(struct result) + (sizeof(uint64_t) * list_len),
		argc - 1);
	if (!results)
		return -ENOMEM;

	// clear any potential dlerrors
	dlerror();

	// load all the modules
	for (i = 0; i < argc - 1; ++i) {
		status = module_init(&results[i].module, argv[i + 1]);
		if (status < 0) {
			fprintf(stderr,
				"failed to load module: %s\n", argv[i + 1]);
			return status;
		}

		results[i].data = (uint64_t*)results
			+ (sizeof(struct result) * (argc - 1))
			+ (i * (sizeof(uint64_t) * list_len));
	}

	// test every puzzle with every module
	for (node = list; node != NULL; node = node->next) {
		for (i = 0; i < (argc - 1); ++i) {
			uint64_t duration;
			status = test(&results[i].module, node->puzzle,
				&duration);
			if (status < 0) {
				continue;
			}

			status = insert(
				results[i].data, results[i].successes,
				list_len, duration);
			if (status < 0)
				return status;

			results[i].successes++;
		}
	}

	// print statistics
	printf("name,author,success,fail,average,stdev,median,min,max\n");

	for (i = 0; i < argc - 1; ++i) {
		uint64_t average, stdev, median, min, max;
		uint64_t* data = results[i].data;
		size_t len = results[i].successes;
		size_t incorrect = results[i].incorrect;
		struct module* module = &results[i].module;

		average = 0;
		stdev = 0;
		median = 0;
		min = 0;
		max = 0;

		if (len > 0) {
			min = data[0];
			max = data[len - 1];
			median = data[len / 2];

			for (j = 0; j < len; ++j)
				average += data[j];

			average = average / len;

			if (len > 1) {
				for (j = 0; j < len; ++j) {
					uint64_t diff = data[j] > average
						? data[j] - average
						: average - data[j];
					stdev += diff * diff;
				}
				stdev /= len - 1;
				stdev = sqrt(stdev);
			}
		}

		printf("%s,%s,%zu,%zu,%zu,%zu,%zu,%zu,%zu\n", module->name,
			module->author, len, list_len - len, average, stdev,
			median, min, max);
	}

	return 0;
}

// valid strings are less than 80 characters long, and contain no newlines or
// commas

bool strlen_less_than(const char* str, size_t max)
{
	int i;

	for (i = 0; i < max; ++i)
		if (str[i] == '\0')
			return true;

	return false;
}

bool valid_string(const char* str)
{
	int i;

	if (!strlen_less_than(str, 80)) {
		printf("strlen not less than\n");
		return false;
	}

	return strpbrk(str, ",") == NULL;
}

int module_init(struct module* module, const char* filename)
{
	char *error;

	module->handle = dlopen(filename, RTLD_NOW | RTLD_LOCAL);
	if (!module->handle)
		return -1;

	module->name = *(char**)dlsym(module->handle, "name");
	error = dlerror();
	if (error != NULL) {
		fprintf(stderr, "%s\n", error);
		return -1;
	}

	module->author = *(char**)dlsym(module->handle, "author");
	error = dlerror();
	if (error != NULL) {
		fprintf(stderr, "%s\n", error);
		return -1;
	}

	module->solve = dlsym(module->handle, "solve");
	error = dlerror();
	if (error != NULL) {
		fprintf(stderr, "%s\n", error);
		return -1;
	}

	if (!valid_string(module->name)) {
		fprintf(stderr, "invalid 'name' string from %s\n", filename);
		return -1;
	}

	if (!valid_string(module->author)) {
		fprintf(stderr, "invalid 'author' string from %s\n", filename);
		return -1;
	}

	return 0;
}

int check(const int* puzzle)
{
	static const iterator_t iterators[] = {
		row_iterator,
		col_iterator,
		cell_iterator
	};

	int status, i, j;

	for (i = 0; i < SUDOKU_SIZE; ++i)
		if (puzzle[i] < 0 || puzzle[i] > 9)
			return -2;

	for (i = 0; i < SUDOKU_AXIS_SIZE; ++i) {
		for (j = 0; j < ARRAY_SIZE(iterators); ++j) {
			status = check_iterator(puzzle, iterators[j], i);
			if (status < 0)
				return status;
		}
	}

	return 0;
}

int cross_check(const int* puzzle, int* solution)
{
	int status, i;

	for (i = 0; i < SUDOKU_SIZE; ++i) {
		if (solution[i] < 1 || solution[i] > 9)
			return -1;

		if (puzzle[i] > 0 && puzzle[i] != solution[i])
			return -1;
	}

	return check(solution);
}

int test(const struct module* module, int* puzzle, uint64_t* duration)
{
	int status;
	struct timespec start, finish;
	int solution[SUDOKU_SIZE];

	memcpy(solution, puzzle, sizeof(solution));

	status = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
	if (status < 0)
		return status;

	status = module->solve(solution);
	if (status < 0) {
		return status;
	}

	status = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &finish);
	if (status < 0)
		return status;

	status = cross_check(puzzle, solution);
	if (status < 0)
		return status;

	*duration = timespec_to_us(&finish) - timespec_to_us(&start);

	return 0;
}

void list_append(struct list_node **list, struct list_node* node)
{
	struct list_node *cursor = *list;

	node->next = NULL;
	if (*list == NULL) {
		*list = node;
		return;
	}

	while (cursor->next != NULL)
		cursor = cursor->next;

	cursor->next = node;
}

size_t list_size(const struct list_node* list)
{
	size_t size = 0;

	for (; list != NULL; list = list->next)
		size++;

	return size;
}

int check_iterator(const int* puzzle, int (*iterator)(int, int), int i)
{
	int j;
	bool flags[SUDOKU_AXIS_SIZE] = {false};

	for (j = 0; j < SUDOKU_AXIS_SIZE; ++j) {
		int value = puzzle[iterator(i, j)];

		if (value > 0) {
			if (flags[value - 1])
				return -1;
			else
				flags[value - 1] = true;
		}
	}

	return 0;
}

int row_iterator(int row, int col)
{
	return (9 * row) + col;
}

int col_iterator(int col, int row)
{
	return row_iterator(row, col);
}

int cell_iterator(int cell, int pos)
{
	return row_iterator(
			((cell / 3) * 3) + (pos / 3),
			((cell % 3) * 3) + (pos % 3));
}

int insert(
	uint64_t *arr, size_t len, size_t max, uint64_t val)
{
	size_t cursor, div;

	if (len >= max)
		return -ERANGE;

	cursor = len / 2;
	for (div = len / 4; div > 0; div /= 2)
		cursor = val > arr[cursor] ? cursor + div : cursor - div;

	memmove(&arr[cursor + 1], &arr[cursor],
		(len - cursor) * sizeof(uint64_t));
	arr[cursor] = val;
	return 0;

}

uint64_t timespec_to_us(struct timespec* val)
{
	return (val->tv_sec * 1000000) + (val->tv_nsec / 1000);
}
