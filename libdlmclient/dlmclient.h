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

/**
 * @file dlmclient.h
 */
#ifndef DLM_CLIENT_H
#define DLM_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief Enable debug logging
 *
 * @param[in] enable enable/disable debug logging
 */
void dlm_enable_debug_log(bool enable);

/**
 * @brief lease handle
 */
struct dlm_lease;

/**
 * @brief  Get a DRM lease from the lease manager
 *
 * @param[in] name requested lease
 * @return A pointer to a lease handle on success.
 *         On error this function returns NULL and errno is set accordingly.
 *
 *  Possible errors:
 *
 *  errno        |  Meaning
 *  -------------|-------------------------------------------------------------
 *  EACCESS      |  Cannot access lease manager socket directory
 *  EACCESS      |  Lease request denied by lease manager
 *  ENAMETOOLONG |  The path to the lease manager socket directory is too long
 *  ENOENT       |  Lease manager or requested lease not available
 *  ENOMEM       |  Out of memory during operation
 *  EPROTO       |  Protocol error in communication with lease manager
 *
 *  This list is not exhaustive, and errno may be set to other error codes,
 *  especially those related to socket communication.
 */
struct dlm_lease *dlm_get_lease(const char *name);

/**
 * @brief  Release a lease handle
 *
 * @details Release a lease handle.  The lease handle will be invalidated and
 *          the associated DRM lease wil be revoked.  Any fd's retrieved from
 *          dlm_lease_fd() will be closed.
 * @param[in] lease pointer to lease handle
 */
void dlm_release_lease(struct dlm_lease *lease);

/**
 * @brief Get a DRM Master fd from a valid lease handle
 *
 * @param[in] lease pointer to a lease handle
 * @return A DRM Master file descriptor for the lease on success.
 *         -1 is returned when called with a NULL lease handle.
 */
int dlm_lease_fd(struct dlm_lease *lease);

#ifdef __cplusplus
}
#endif

#endif
