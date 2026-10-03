#include <stdint.h>
#include <unistd.h>

int main(void) {
    for(;;) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe) this deliberately blocks until killed.
        sleep(UINT64_MAX);
    }
}
