/*
 * dpms_test - query and set the screen DPMS state through app_server.
 *
 * usage:
 *	dpms_test				print capabilities and current state
 *	dpms_test off|suspend|standby|on
 *	dpms_test cycle <mode> <seconds>	set <mode>, wait, then turn back on
 */


#include <Application.h>
#include <Screen.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static const char*
dpms_name(uint32 state)
{
	switch (state) {
		case B_DPMS_ON:
			return "on";
		case B_DPMS_STAND_BY:
			return "standby";
		case B_DPMS_SUSPEND:
			return "suspend";
		case B_DPMS_OFF:
			return "off";
	}
	return "unknown";
}


static bool
parse_dpms(const char* name, uint32& state)
{
	if (strcmp(name, "on") == 0)
		state = B_DPMS_ON;
	else if (strcmp(name, "standby") == 0)
		state = B_DPMS_STAND_BY;
	else if (strcmp(name, "suspend") == 0)
		state = B_DPMS_SUSPEND;
	else if (strcmp(name, "off") == 0)
		state = B_DPMS_OFF;
	else
		return false;
	return true;
}


int
main(int argc, char** argv)
{
	BApplication app("application/x-vnd.radeon-polaris-dpms-test");
	BScreen screen;

	uint32 capabilities = screen.DPMSCapabilites();
	printf("capabilities:%s%s%s%s\n",
		(capabilities & B_DPMS_ON) != 0 ? " on" : "",
		(capabilities & B_DPMS_STAND_BY) != 0 ? " standby" : "",
		(capabilities & B_DPMS_SUSPEND) != 0 ? " suspend" : "",
		(capabilities & B_DPMS_OFF) != 0 ? " off" : "");
	printf("state: %s\n", dpms_name(screen.DPMSState()));

	if (argc < 2)
		return 0;

	uint32 state;
	if (strcmp(argv[1], "cycle") == 0 && argc >= 4
		&& parse_dpms(argv[2], state)) {
		printf("set %s: %s\n", argv[2], strerror(screen.SetDPMS(state)));
		sleep(atoi(argv[3]));
		printf("set on: %s\n", strerror(screen.SetDPMS(B_DPMS_ON)));
	} else if (parse_dpms(argv[1], state)) {
		printf("set %s: %s\n", argv[1], strerror(screen.SetDPMS(state)));
	} else {
		fprintf(stderr, "unknown mode %s\n", argv[1]);
		return 1;
	}

	printf("state: %s\n", dpms_name(screen.DPMSState()));
	return 0;
}
