SUMMARY = "Configuration file for drm-lease-manager"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

PACKAGE_ARCH = "${MACHINE_ARCH}"

SRC_URI = "file://drm-lease-manager.ini"

DRM_LEASE_MANAGER_CONF_DIR := "${sysconfdir}/xdg/drm-lease-manager"

S = "${WORKDIR}"

do_install() {
    install -d ${D}${DRM_LEASE_MANAGER_CONF_DIR}
    install -m644 ${WORKDIR}/drm-lease-manager.ini ${D}/${DRM_LEASE_MANAGER_CONF_DIR}/drm-lease-manager.ini
}

RDEPENDS:${PN} = "drm-lease-manager"
