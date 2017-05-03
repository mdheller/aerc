#define _POSIX_C_SOURCE 199309L
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include "subprocess.h"
#include "tests.h"

static void _subp_wait(struct subprocess *subp) {
	struct timespec now, end, sleep = { 0, .5e+8 };
	clock_gettime(CLOCK_MONOTONIC, &end);
	end.tv_nsec += 3e+9; // 3 second timeout
	do {
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (subprocess_update(subp)) {
			return;
		}
		nanosleep(&sleep, NULL);
	} while (now.tv_nsec < end.tv_nsec);
	assert_true(false);
}

static void test_basic_subprocess(void **state) {
	char *argv[] = { "true" };
	struct subprocess *subp = subprocess_init(argv, false);
	assert_non_null(subp);

	subprocess_start(subp);
	_subp_wait(subp);
	subprocess_free(subp);
}

static void test_capture_stdout(void **state) {
	char *argv[] = { "echo", "hello world" };
	struct subprocess *subp = subprocess_init(argv, false);
	assert_non_null(subp);

	subprocess_capture_stdout(subp);
	subprocess_start(subp);
	_subp_wait(subp);
	assert_int_equal(strlen(argv[1]) + 1 /* \n */, subp->io_stdout->len);
	assert_memory_equal(argv[1], subp->io_stdout->data, strlen(argv[1]));

	subprocess_free(subp);
}

static void test_capture_stderr(void **state) {
	const char *hello_world = "hello world";
	char *argv[] = { "sh", "-c", "echo 'hello world' 1>&2" };
	struct subprocess *subp = subprocess_init(argv, false);
	assert_non_null(subp);

	subprocess_capture_stderr(subp);
	subprocess_start(subp);
	_subp_wait(subp);
	assert_int_equal(strlen(hello_world) + 1 /* \n */, subp->io_stderr->len);
	assert_memory_equal(hello_world, subp->io_stderr->data, strlen(hello_world));

	subprocess_free(subp);
}

static void test_queue_stdin(void **state) {
	const char *data = "hello world";
	char *argv[] = { "cat" };
	struct subprocess *subp = subprocess_init(argv, false);
	assert_non_null(subp);

	subprocess_capture_stdout(subp);
	subprocess_queue_stdin(subp, (uint8_t *)data, strlen(data));
	subprocess_start(subp);
	_subp_wait(subp);
	assert_int_equal(strlen(data), subp->io_stdout->len);
	assert_memory_equal(data, subp->io_stdout->data, strlen(data));

	subprocess_free(subp);
}

static void test_queue_stdin_many(void **state) {
	const char *data = "hello world";
	char *argv[] = { "cat" };
	struct subprocess *subp = subprocess_init(argv, false);
	assert_non_null(subp);

	subprocess_capture_stdout(subp);
	subprocess_queue_stdin(subp, (uint8_t *)&data[0], strlen("hello "));
	subprocess_queue_stdin(subp, (uint8_t *)&data[strlen("hello ")], strlen("world"));
	subprocess_start(subp);
	_subp_wait(subp);
	assert_int_equal(strlen(data), subp->io_stdout->len);
	assert_memory_equal(data, subp->io_stdout->data, strlen(data));

	subprocess_free(subp);
}

int run_tests_subprocess() {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_basic_subprocess),
		cmocka_unit_test(test_capture_stdout),
		cmocka_unit_test(test_capture_stderr),
		cmocka_unit_test(test_queue_stdin),
		cmocka_unit_test(test_queue_stdin_many),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
