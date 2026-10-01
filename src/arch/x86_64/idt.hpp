#pragma once

namespace idt {

/*
 * Install interrupt gates for the x86 CPU exception vectors and the generic
 * external-interrupt vector range, then load the resulting Interrupt Descriptor
 * Table. Serial output must already be initialized because exception diagnostics use it.
 */
void initialize();

} // namespace idt
