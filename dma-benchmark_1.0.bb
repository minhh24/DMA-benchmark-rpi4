SUMMARY = "DMA vs. memcpy benchmark with GPIO pulse output"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Giả định file C của bạn tên là dma_benchmark.c
# và nằm trong thư mục 'files/' bên cạnh recipe này
SRC_URI = "file://dma_benchmark.c"

S = "${WORKDIR}"

# Sửa lỗi "gpiod.h: No such file or directory"
DEPENDS += "libgpiod"

# Đảm bảo libgpiod.so được cài lên image
# (Sửa lỗi "error while loading shared libraries" trên Pi)
RDEPENDS_${PN} += "libgpiod"

# Sửa hàm compile theo mẫu của bạn
# Thêm -lgpiod (cho GPIO) và -lrt (cho clock_gettime)
do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} dma_benchmark.c -o dma-benchmark -lgpiod -lrt
}

# Sửa hàm install để cài file 'dma-benchmark'
do_install() {
    install -d ${D}${bindir}
    install -m 0755 dma-benchmark ${D}${bindir}/
}