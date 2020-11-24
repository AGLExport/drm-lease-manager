# DRM Lease Manager

The DRM Lease Manager uses the DRM Lease feature, introduced in the Linux kernel version 4.15,
to partition display controller output resources between multiple processes.

For more information on the DRM lease functionality, please see the blog posts published by Keith
Packard, who developed the feature, at https://keithp.com/blogs/DRM-lease/

This repository contains a user space daemon to create and manage DRM leases, and distribute them
to client applications.  This implementation is independent of any window system / compositor,
such as X11 or wayland, so can be used with clients that directly access a DRM device.

This repository also provides a library that clients can use to communicate with the DRM Lease Manager.

## Building

Build this repository requires a recent version of the [meson](https://mesonbuild.com/Getting-meson.html) build system.

The basic build procedure is as follows:

    meson <build_dir>
    ninja -C <build_dir>
    sudo ninja -C <build_dir> install

`<build_dir>` can be any directory name, but `build` is commonly used.

## Running

Once installed, running the following command will start the DRM Lease Manager daemon

    drm-lease-manager [<path DRM device>]

If no DRM device is specified, `/dev/dri/card0` will be used.  
More detailed options can be displayed by specifying the `-h` flag.

### Lease naming

One DRM lease will be created for each connector on the DRM device (up to the number of available CRTCs).

The names of the DRM leases will have the following pattern:

    <device>-<connector name>

So, for example, a DRM lease for the first LVDS device on the device `/dev/dri/card0` would be named
`card0-LVDS-1`.

## Client API usage

The libdmclient handles all communication with the DRM Lease Manager and provides file descriptors that
can be used as if the DRM device was opened directly. Clients only need to replace their calls to
`drmOpen()` and `drmClose()` with the appropriate libdlmclient API calls.

The client library API is described in `dlmclient.h` in the `libdlmclient` directory.

If doxygen is available, building the repository will generate doxygen documentation in the
`<build_dir>/libdlmclient/docs/html` directory.

### Examples

_Error handling has been omitted for brevity and clarity of examples._

#### Requesting a lease from the DRM Lease Manager

```c
  struct dlm_lease *lease = dlm_get_lease("card0-HDMI-A-1");
  int drm_device_fd = dlm_lease_fd(lease);
```

`drm_device_fd` can now be used to access the DRM device

#### Releasing a lease

```c
  dlm_release_lease(lease);
```

**Note: `drm_device_fd` is not usable after calling `dlm_release_lease()`**

## Runtime directory
A runtime directory under the `/var` system directory is used by the drm-lease-manager and clients to
communicate with each other.  
The default path is `/var/run/drm-lease-manager`, but can be changed by setting the `-Druntime_subdir`
option during configuration with `meson`.

The runtime directory can also be specified at runtime by setting the `DLM_RUNTIME_PATH` environment variable.
