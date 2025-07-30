#include "wrc.h"
#include "shell.h"
#include "cmds.h"

int cmd_uptime(const char *args[])
{
	extern uint32_t uptime_sec;

	pp_printf("%u\n", (unsigned int) uptime_sec);
	return 0;
}
