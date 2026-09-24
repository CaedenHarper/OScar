#include <limine.h>
#include <stdint.h>

namespace {

constexpr uint16_t kCom1 = 0x3f8;

inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init() {
    outb(kCom1 + 1, 0x00); // Disable interrupts.
    outb(kCom1 + 3, 0x80); // Enable divisor latch.
    outb(kCom1 + 0, 0x03); // 38400 baud divisor, low byte.
    outb(kCom1 + 1, 0x00); // Divisor, high byte.
    outb(kCom1 + 3, 0x03); // 8 data bits, no parity, one stop bit.
    outb(kCom1 + 2, 0xc7); // Enable and clear the FIFO.
    outb(kCom1 + 4, 0x0b); // Enable IRQs and mark the terminal ready.
}

void serial_putc(char character) {
    if (character == '\n') {
        serial_putc('\r');
    }

    while ((inb(kCom1 + 5) & 0x20) == 0) {
        asm volatile("pause");
    }
    outb(kCom1, static_cast<uint8_t>(character));
}

void serial_write(const char* text) {
    while (*text != '\0') {
        serial_putc(*text++);
    }
}

void serial_write_u64(uint64_t value) {
    char digits[21];
    unsigned length = 0;

    do {
        digits[length++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0);

    while (length != 0) {
        serial_putc(digits[--length]);
    }
}

[[noreturn]] void halt() {
    for (;;) {
        asm volatile("cli; hlt");
    }
}

__attribute__((used, section(".limine_requests")))
volatile uint64_t g_limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
volatile limine_memmap_request g_memory_map_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = nullptr,
};

__attribute__((used, section(".limine_requests_start")))
volatile uint64_t g_limine_requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
volatile uint64_t g_limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;

} // namespace

extern "C" [[noreturn]] void kmain() {
    serial_init();
    serial_write("Barebones kernel started.\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(g_limine_base_revision)) {
        serial_write("ERROR: unsupported Limine base revision.\n");
        halt();
    }

    if (g_memory_map_request.response == nullptr) {
        serial_write("ERROR: Limine did not provide a memory map.\n");
        halt();
    }

    serial_write("Memory-map entries: ");
    serial_write_u64(g_memory_map_request.response->entry_count);
    serial_write("\nKernel initialization complete; halting.\n");

    halt();
}

