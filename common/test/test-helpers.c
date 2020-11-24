/* Copyright 2020-2021 IGEL Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "test-helpers.h"
#include <check.h>

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

int get_dummy_fd(void)
{
	return dup(STDIN_FILENO);
}

void check_fd_equality(int fd1, int fd2)
{
	struct stat s1, s2;
	ck_assert_int_eq(fstat(fd1, &s1), 0);
	ck_assert_int_eq(fstat(fd2, &s2), 0);
	ck_assert_int_eq(s1.st_dev, s2.st_dev);
	ck_assert_int_eq(s1.st_ino, s2.st_ino);
}

void check_fd_is_open(int fd)
{
	struct stat st;
	ck_assert_int_eq(fstat(fd, &st), 0);
}

void check_fd_is_closed(int fd)
{
	struct stat st;
	ck_assert_int_ne(fstat(fd, &st), 0);
	ck_assert_int_eq(errno, EBADF);
}

void check_uint_array_eq(const uint32_t *a, const uint32_t *b, int cnt)
{
	for (int i = 0; i < cnt; i++) {
		ck_assert_msg(a[i] == b[i], "Array diff at index %d (%u != %u)",
			      i, a[i], b[i]);
	}
}
