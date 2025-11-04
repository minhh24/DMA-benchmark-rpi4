/*
 * LAB: Compare DMA (Hardware) vs. memcpy (CPU) Speed
 *
 * OBJECTIVES:
 * - Compare the speed of RAM-to-RAM memory copies.
 * - Use 2 GPIO pins to output pulses for timing with a Logic Analyzer.
 *
 * PROGRAM FLOW:
 * 1. Initialize 2 GPIO pins (17 for memcpy, 18 for DMA) as outputs
 * 2. Read the user-created file 'dma_src.txt' to get its size.
 * 3. Request 2 contiguous physical memory blocks (src, dst) from the Kernel via Mailbox.
 * 4. Read the entire 'dma_src.txt' file into the 'src' memory block.
 * 5. Loop 10 times:
 * a. Set GPIO 17 HIGH, run memcpy() (CPU copy), Set GPIO 17 LOW.
 * b. Set GPIO 18 HIGH, run DMA (Hardware copy), Set GPIO 18 LOW.
 * 6. Print the measured time results.
 * 7. Write the contents of the 'dst' memory block to 'dma_dst.txt' for verification.
 * 8. Clean up all resources.
 *
 * Build: Must link with -lrt (for time) and -lgpiod (for GPIO).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>   // Needed for mmap()
#include <sys/ioctl.h>  // Needed for ioctl()
#include <sys/stat.h>   // Needed for stat() (get file size)
#include <time.h>       // Needed for clock_gettime()
#include <errno.h>
#include <gpiod.h>      // Needed for GPIO control

// ----- Hardware Config (Pi 4) -----
#define BCM_PERI_BASE           0xFE000000
#define DMA_BASE                (BCM_PERI_BASE + 0x007000)
#define DMA_LEN                 0x1000
#define PAGE_SIZE               4096
#define DMA_CHAN                5 // Use DMA Channel 5

// ----- GPIO Config -----
#define GPIO_CHIP_NAME          "gpiochip0"
#define PIN_MEMCPY              17 // Channel for memcpy
#define PIN_DMA                 18 // Channel for DMA

// ----- Mailbox Config (Communication with GPU) -----
#define IOCTL_MBOX_PROPERTY     _IOWR(100, 0, char *)
// Request special memory: Non-cached, contiguous
#define MEM_FLAG_DIRECT         (1 << 2)
#define MEM_FLAG_COHERENT       (2 << 2)
#define MEM_FLAG_L1_NONALLOCATING (MEM_FLAG_DIRECT | MEM_FLAG_COHERENT)

// ----- DMA Hardware Structures -----
// 1. "Control Block" (CB)
//    This is the "instruction sheet" the CPU fills out for the DMA to read.
typedef struct {
    uint32_t ti;        // Transfer info (e.g., increment src/dest address)
    uint32_t source_ad; // Source address (BUS address)
    uint32_t dest_ad;   // Destination address (BUS address)
    uint32_t txfr_len;  // Length
    uint32_t stride;
    uint32_t next_cb;   // Next CB (if any)
    uint32_t rsvd[2];
} __attribute__((aligned(32))) dma_cb_t;

// 2. DMA Channel Registers
//    These are the "buttons" the CPU presses to command the DMA.
typedef struct {
    uint32_t cs;        // Control & Status Register (Enable/Disable/Reset)
    uint32_t conblk_ad; // Address of the Control Block (CB)
} dma_chan_regs_t;

// DMA Control Flags
#define DMA_TI_SRC_INC          (1 << 8) // Automatically increment source address
#define DMA_TI_DEST_INC         (1 << 4) // Automatically increment destination address
#define DMA_CS_RESET            (1U << 31)// Flag to reset the channel
#define DMA_CS_ACTIVE           (1 << 0) // Flag to activate (enable) DMA

// ----- Lab Config -----
#define NUM_RUNS 10
#define SRC_FILENAME "dma_src.txt"
#define DST_FILENAME "dma_dst.txt"

// Global variable for the Mailbox file descriptor
static int mbox_fd = -1;

/*
 * ----- Mailbox Function Group (Complex, students just need to know their function) -----
 * These are low-level functions to talk to the GPU to request physical memory.
 */

// Function: Request 'size' bytes of contiguous memory from the GPU
uint32_t mem_alloc(uint32_t size, uint32_t align, uint32_t flags) {
    uint32_t mbox_buf[32] __attribute__((aligned(16)));
    mbox_buf[0] = 9 * 4; // Packet size (36 bytes)
    mbox_buf[1] = 0;
    mbox_buf[2] = 0x3000c; // Tag: Allocate Memory
    mbox_buf[3] = 12;
    mbox_buf[4] = 0;
    mbox_buf[5] = size;
    mbox_buf[6] = align;
    mbox_buf[7] = flags;
    mbox_buf[8] = 0; // End tag

    if (ioctl(mbox_fd, IOCTL_MBOX_PROPERTY, mbox_buf) < 0) {
        perror("ioctl mem_alloc");
        return 0;
    }
    return mbox_buf[5]; // Returns a "handle" (ID of the memory block)
}

// Function: Lock the memory block and get its "Bus Address"
// The DMA is hardware; it doesn't understand virtual addresses.
// It needs the physical/bus address.
uint32_t mem_lock(uint32_t handle) {
    uint32_t mbox_buf[32] __attribute__((aligned(16)));
    mbox_buf[0] = 8 * 4;
    mbox_buf[1] = 0;
    mbox_buf[2] = 0x3000d; // Tag: Lock memory
    mbox_buf[3] = 4;
    mbox_buf[4] = 0;
    mbox_buf[5] = handle;
    mbox_buf[6] = 0;
    mbox_buf[7] = 0; 

    if (ioctl(mbox_fd, IOCTL_MBOX_PROPERTY, mbox_buf) < 0) {
        perror("ioctl mem_lock");
        return 0;
    }
    return mbox_buf[5]; // Returns the "Bus Address"
}

// Function: Release the memory back to the GPU
void mem_free(uint32_t handle) {
    uint32_t mbox_buf[32] __attribute__((aligned(16)));
    mbox_buf[0] = 8 * 4;
    mbox_buf[1] = 0;
    mbox_buf[2] = 0x3000f; // Tag: Free memory
    mbox_buf[3] = 4;
    mbox_buf[4] = 0;
    mbox_buf[5] = handle;
    mbox_buf[6] = 0;
    mbox_buf[7] = 0; 

    if (ioctl(mbox_fd, IOCTL_MBOX_PROPERTY, mbox_buf) < 0) {
        perror("ioctl mem_free");
    }
}

// Get time in nanoseconds (requires linking -lrt)
long long get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// ----- MAIN FUNCTION -----
int main() {
    if (getuid() != 0) {
        fprintf(stderr, "Error: Must be run as root.\n");
        return 1;
    }

    // ----- 1. INITIALIZE GPIO -----
    struct gpiod_chip *chip;
    struct gpiod_line *line_memcpy;
    struct gpiod_line *line_dma;

    chip = gpiod_chip_open_by_name(GPIO_CHIP_NAME);
    if (!chip) {
        perror("Failed to open GPIO chip");
        return 1;
    }

    line_memcpy = gpiod_chip_get_line(chip, PIN_MEMCPY);
    line_dma = gpiod_chip_get_line(chip, PIN_DMA);
    if (!line_memcpy || !line_dma) {
        perror("Failed to get GPIO line 17 or 18");
        goto cleanup_gpio_init;
    }
    
    if (gpiod_line_request_output(line_memcpy, "memcpy-bench", 0) < 0 ||
        gpiod_line_request_output(line_dma, "dma-bench", 0) < 0) {
        perror("Failed to request GPIO line as output");
        goto cleanup_gpio_init;
    }
    printf("Initialized GPIO %d (memcpy) and GPIO %d (DMA).\n", PIN_MEMCPY, PIN_DMA);

    // ----- 2. GET FILE SIZE -----
    struct stat st;
    if (stat(SRC_FILENAME, &st) != 0) {
        perror("Error: Could not find 'dma_src.txt'. Did you create it?");
        goto cleanup_gpio;
    }
    size_t content_len = st.st_size; // This is the number of bytes to copy
    if (content_len == 0) {
        fprintf(stderr, "Error: 'dma_src.txt' is empty.\n");
        goto cleanup_gpio;
    }

    printf("--- Starting DMA vs. memcpy Lab ---\n");
    printf("Source file: '%s' (Size: %zu bytes)\n\n", SRC_FILENAME, content_len);

    // Open necessary device files
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    mbox_fd = open("/dev/vcio", 0);
    if (mem_fd < 0 || mbox_fd < 0) {
        perror("Error: Could not open /dev/mem or /dev/vcio (requires root)");
        goto cleanup_gpio;
    }

    // ----- 3. ALLOCATE MEMORY (The complex part) -----
    
    // 3a. Map the DMA register region
    void *dma_virt_addr = mmap(NULL, DMA_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, DMA_BASE);
    if (dma_virt_addr == MAP_FAILED) {
        perror("mmap DMA registers failed");
        goto cleanup_fds;
    }
    // Point 'dma_regs' to our specific channel (DMA 5)
    volatile dma_chan_regs_t *dma_regs = (dma_chan_regs_t *)(dma_virt_addr + 0x100 * DMA_CHAN);

    // 3b. Allocate memory for 3 things: CB, Source, Destination
    // Align the allocation size to a full page
    size_t alloc_len = (content_len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    uint32_t cb_handle = mem_alloc(PAGE_SIZE, PAGE_SIZE, MEM_FLAG_L1_NONALLOCATING);
    uint32_t src_handle = mem_alloc(alloc_len, PAGE_SIZE, MEM_FLAG_L1_NONALLOCATING);
    uint32_t dst_handle = mem_alloc(alloc_len, PAGE_SIZE, MEM_FLAG_L1_NONALLOCATING);

    if (!cb_handle || !src_handle || !dst_handle) {
        fprintf(stderr, "Mailbox memory allocation failed (Maybe file is too large?)\n");
        goto cleanup_dma_map;
    }

    // 3c. Get BUS addresses (for the DMA)
    uint32_t cb_bus = mem_lock(cb_handle);
    uint32_t src_bus = mem_lock(src_handle);
    uint32_t dst_bus = mem_lock(dst_handle);

    // 3d. Get VIRTUAL addresses (for the CPU)
    // The CPU uses mmap() to "see" the memory the GPU allocated
    dma_cb_t* cb_virt = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, cb_bus & ~0xC0000000);
    void* src_virt = mmap(NULL, alloc_len, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, src_bus & ~0xC0000000);
    void* dst_virt = mmap(NULL, alloc_len, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, dst_bus & ~0xC0000000);

    if (src_virt == MAP_FAILED || dst_virt == MAP_FAILED || cb_virt == MAP_FAILED) {
        perror("mmap Mailbox memory failed");
        goto cleanup_handles;
    }

    // ----- 4. READ FILE INTO RAM -----
    printf("Reading '%s' (%zu bytes) into RAM...\n", SRC_FILENAME, content_len);
    FILE *f_in = fopen(SRC_FILENAME, "rb"); 
    if (!f_in) {
        perror("Failed to open source file for reading");
        goto cleanup_virt_maps;
    }
    size_t bytes_read = fread(src_virt, 1, content_len, f_in);
    fclose(f_in);
    if (bytes_read != content_len) {
        fprintf(stderr, "Error: Read %zu bytes, but file has %zu bytes.\n", bytes_read, content_len);
        goto cleanup_virt_maps;
    }
    printf("Read complete. Starting benchmark...\n\n");

    long long dma_times[NUM_RUNS];
    long long memcpy_times[NUM_RUNS];
    long long start_ns, end_ns;

    // ----- 5. RUN BENCHMARK -----
    for (int i = 0; i < NUM_RUNS; i++) {
        
        // --- Test 1: memcpy (CPU) ---
        memset(dst_virt, 0, content_len); // Clear destination

        gpiod_line_set_value(line_memcpy, 1); // PULSE 17 HIGH
        start_ns = get_time_ns();
        
        memcpy(dst_virt, src_virt, content_len); // CPU does the work
        
        end_ns = get_time_ns();
        gpiod_line_set_value(line_memcpy, 0); // PULSE 17 LOW
        
        memcpy_times[i] = end_ns - start_ns;
        
        if (memcmp(src_virt, dst_virt, content_len) != 0) {
            printf("Run %d: memcpy FAILED!\n", i + 1);
        }

        // --- Test 2: DMA (Hardware) ---
        memset(dst_virt, 0, content_len); // Clear destination

        // 5b-1. Fill out the "instruction sheet" (Control Block)
        cb_virt->ti = DMA_TI_SRC_INC | DMA_TI_DEST_INC; // Increment src, increment dest
        cb_virt->source_ad = src_bus;    // Source bus address
        cb_virt->dest_ad = dst_bus;      // Destination bus address
        cb_virt->txfr_len = content_len; // How much to copy
        cb_virt->stride = 0;
        cb_virt->next_cb = 0;            // No next CB

        gpiod_line_set_value(line_dma, 1); // PULSE 18 HIGH
        start_ns = get_time_ns();

        // 5b-2. Press the "buttons" (Manipulate registers)
        dma_regs->cs = DMA_CS_RESET;     // Reset channel
        usleep(10);                      // Wait for reset
        dma_regs->conblk_ad = cb_bus;    // Load the "instruction sheet"
        dma_regs->cs = DMA_CS_ACTIVE;    // Press "GO"

        // 5b-3. Wait for DMA to finish
        while (dma_regs->cs & DMA_CS_ACTIVE) {
            // CPU is free, just polling for the ACTIVE flag to clear
        }

        end_ns = get_time_ns();
        gpiod_line_set_value(line_dma, 0); // PULSE 18 LOW

        dma_times[i] = end_ns - start_ns;

        if (memcmp(src_virt, dst_virt, content_len) != 0) {
            printf("Run %d: DMA FAILED!\n", i + 1);
        }
        
        printf("Finished run %d/%d (memcpy: %lld ns, dma: %lld ns)\n", 
               i + 1, NUM_RUNS, memcpy_times[i], dma_times[i]);
        
        usleep(50 * 1000); // Sleep 50ms to make pulses distinct on Logic Analyzer
    }

    // ----- 6. PRINT RESULTS -----
    printf("\n--- BENCHMARK RESULTS (nanoseconds) ---\n");
    printf("Run\t           | DMA Time\t    | memcpy Time\t   | Faster\n");
    printf("----------------|---------------|-----------------|----------\n");
    for (int i = 0; i < NUM_RUNS; i++) {
        printf("%-15d | %-13lld | %-15lld | %s\n",
               i + 1,
               dma_times[i],
               memcpy_times[i],
               (dma_times[i] < memcpy_times[i]) ? "DMA" : "memcpy");
    }

    // ----- 7. WRITE VERIFICATION FILE -----
    printf("\nWriting result to '%s'...\n", DST_FILENAME);
    FILE *f_out = fopen(DST_FILENAME, "wb"); 
    if (!f_out) {
        perror("Failed to open destination file for writing");
        goto cleanup_virt_maps;
    }
    size_t bytes_written = fwrite(dst_virt, 1, content_len, f_out);
    fclose(f_out);
    if (bytes_written != content_len) {
        fprintf(stderr, "Error: Wrote %zu bytes, but expected %zu.\n", bytes_written, content_len);
    } else {
        printf("Write complete. Use 'md5sum' to compare the two files.\n");
    }

    // ----- 8. CLEAN UP -----
    // Clean up in reverse order of initialization
cleanup_virt_maps:
    munmap(cb_virt, PAGE_SIZE);
    munmap(src_virt, alloc_len);
    munmap(dst_virt, alloc_len);
cleanup_handles:
    if (cb_handle) mem_free(cb_handle);
    if (src_handle) mem_free(src_handle);
    if (dst_handle) mem_free(dst_handle);
cleanup_dma_map:
    munmap(dma_virt_addr, DMA_LEN);
cleanup_fds:
    close(mbox_fd);
    close(mem_fd);
cleanup_gpio:
    gpiod_line_release(line_memcpy);
    gpiod_line_release(line_dma);
cleanup_gpio_init:
    gpiod_chip_close(chip);

    printf("--- End of Lab ---\n");
    return 0;
}
