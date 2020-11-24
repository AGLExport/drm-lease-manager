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

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stdint.h>

#define UNUSED(x) (void)(x)
#define ARRAY_LEN(x) ((int)(sizeof(x) / sizeof(x[0])))

/* Get a vaild fd to use a a placeholder.
 * The dummy fd should never be used for anything other
 * than comparing the fd value or the referenced file description. */
int get_dummy_fd(void);

void check_fd_equality(int fd1, int fd2);
void check_fd_is_open(int fd);
void check_fd_is_closed(int fd);

void check_uint_array_eq(const uint32_t *a, const uint32_t *b, int cnt);
#endif
