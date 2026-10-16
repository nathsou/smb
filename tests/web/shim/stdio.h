// See assert.h: messages go to the host.
#pragma once
void fixture_pass(const char *message);
#define puts fixture_pass
