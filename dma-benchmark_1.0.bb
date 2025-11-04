SUMMARY = "DMA vs. memcpy benchmark with GPIO pulse output"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://dma_benchmark.c"

S = "${WORKDIR}"
DEPENDS += "libgpiod"

RDEPENDS_${PN} += "libgpiod"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} dma_benchmark.c -o dma-benchmark -lgpiod -lrt
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 dma-benchmark ${D}${bindir}/
}
