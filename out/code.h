#ifndef SMB_CODE_H
#define SMB_CODE_H

#include "data.h"
#include "instructions.h"

typedef enum { RUN_STATE_RESET, RUN_STATE_NMI_HANDLER } RunState;

void smb(RunState state);

#endif
