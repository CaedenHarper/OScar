#pragma once

namespace idt {

/*
 * Install interrupt gates for the first 32 x86 CPU exception vectors and the
 * timer IRQ vector, then load the resulting Interrupt Descriptor Table. Serial
 * output must already be initialized because exception diagnostics use it.
 */
void initialize();

} // namespace idt
