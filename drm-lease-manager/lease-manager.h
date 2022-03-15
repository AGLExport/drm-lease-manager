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

#ifndef LEASE_MANAGER_H
#define LEASE_MANAGER_H
#include "drm-lease.h"

struct lm;

struct lm *lm_create(const char *path);
struct lm *lm_create_with_config(const char *path, int leases,
				 struct lease_config *configs);

void lm_destroy(struct lm *lm);

int lm_get_lease_handles(struct lm *lm, struct lease_handle ***lease_handles);

int lm_lease_grant(struct lm *lm, struct lease_handle *lease_handle);
int lm_lease_transfer(struct lm *lm, struct lease_handle *lease_handle);
void lm_lease_revoke(struct lm *lm, struct lease_handle *lease_handle);
void lm_lease_close(struct lease_handle *lease_handle);
#endif
