//

#pragma once

const char* OS_Signal_ToString(int signal);

/* returns first caught signal. 0 = no signal */
int OS_Signal_GetSignal();

/* installs the signal handler. may return false. */
bool OS_Signal_Install();

/* confirms with the OS that we're ready to exit. omitting this call, the OS will timeout our process. */
void OS_Signal_Uninstall();
