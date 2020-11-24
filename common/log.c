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

#include "log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

static bool debug_log = false;

void dlm_log_enable_debug(bool enable)
{
	debug_log = enable;
}

void dlm_log_print(bool debug, FILE *stream, char *fmt, ...)
{
	if (debug && !debug_log)
		return;

	va_list argl;
	va_start(argl, fmt);
	vfprintf(stream, fmt, argl);
	va_end(argl);
}
