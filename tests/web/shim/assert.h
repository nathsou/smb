// Freestanding WebAssembly builds of the C fixture harnesses (see
// tests/web/fixture.mjs): a failed assertion reports its line to the host.
#pragma once
void fixture_fail(int line);
#define assert(condition) ((condition) ? (void)0 : fixture_fail(__LINE__))
