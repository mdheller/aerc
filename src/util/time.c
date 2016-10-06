#include "util/time.h"

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#endif

void get_nanoseconds(struct timespec *tp) {
	// http://stackoverflow.com/questions/5167269/clock-gettime-alternative-in-mac-os-x#6725161
	#ifdef __MACH__
	clock_serv_t cclock;
	mach_timespec_t mts;
	host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
	clock_get_time(cclock, &mts);
	mach_port_deallocate(mach_task_self(), cclock);
	tp->tv_sec = mts.tv_sec;
	tp->tv_nsec = mts.tv_nsec;
	#else
	clock_gettime(CLOCK_MONOTONIC, tp);
	#endif

	(void) tp;
}
