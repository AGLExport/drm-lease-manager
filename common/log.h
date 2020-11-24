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

#ifndef LOG_H
#define LOG_H

#include <stdbool.h>
#include <stdio.h>

#define DEBUG_LOG(FMT, ...) \
	dlm_log_print(true, stdout, "DEBUG: %s: " FMT, __func__, ##__VA_ARGS__)
#define INFO_LOG(FMT, ...) \
	dlm_log_print(false, stdout, "INFO: " FMT, ##__VA_ARGS__)
#define WARN_LOG(FMT, ...) \
	dlm_log_print(false, stderr, "WARNING: " FMT, ##__VA_ARGS__)
#define ERROR_LOG(FMT, ...) \
	dlm_log_print(false, stderr, "ERROR: " FMT, ##__VA_ARGS__)

void dlm_log_enable_debug(bool enable);
void dlm_log_print(bool debug, FILE *stream, char *fmt, ...);
#endif
