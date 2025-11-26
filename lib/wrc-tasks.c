#include <stdint.h>
#include <string.h>

#include "assert.h"
#include "wrc.h"
#include "wrc-task.h"
#include "dev/pps_gen.h"
#include "tasks.h"

#ifndef CONFIG_DEFAULT_PRINT_TASK_TIME_THRESHOLD
	#define CONFIG_DEFAULT_PRINT_TASK_TIME_THRESHOLD 0
#endif

static uint32_t prev_nanos_for_profile;
static uint32_t prev_ticks_for_profile;
uint32_t print_task_time_threshold = CONFIG_DEFAULT_PRINT_TASK_TIME_THRESHOLD;

struct wrc_task {
	const char *name;
	int (*enabled)(void);
	void (*init)(void);
	int (*job)(void);
};

static const struct wrc_task tasks[] =
  {
#undef DEF_TASK
#define NO_INIT NULL
#define NO_ENABLED NULL
#define NO_JOB NULL
#define DEF_TASK(NAME, INIT, JOB, ENABLED)	\
    { NAME, ENABLED, INIT, JOB },
#include "tasks.h"
  };

#define WRC_NBR_TASKS ARRAY_SIZE(tasks)

static struct wrc_task_usage tasks_usage[WRC_NBR_TASKS];

static void task_time_normalize(struct wrc_task_usage *t)
{
	if (t->nanos > 1000 * 1000 * 1000) {
		t->nanos -= 1000 * 1000 * 1000;
		t->seconds++;
	}
}

/* Account the time to either this task or task 0 */
static void account_task(unsigned tid, int done_sth)
{
	struct wrc_task_usage *t = &tasks_usage[tid];
	uint32_t nanos;
	signed int delta;
	uint32_t ticks;
	signed int delta_ticks;

	if (!done_sth)
		t = &tasks_usage[0]; /* task 0 is special */
	shw_pps_gen_get_time(NULL, &nanos);
	/* get monotonic number of ticks */
	ticks = timer_get_tics();

	delta = nanos - prev_nanos_for_profile;
	if (delta < 0)
		delta += 1000 * 1000 * 1000;

	t->nanos += delta;
	task_time_normalize(t);
	prev_nanos_for_profile = nanos;

	delta_ticks = ticks - prev_ticks_for_profile;
	if (delta_ticks < 0)
		delta_ticks += TICS_PER_SECOND;

	if (t->max_run_ticks < delta_ticks) {/* update max_run_ticks */
		if (print_task_time_threshold) {
			/* Print only if threshold is set */
			pp_printf("New max run time for a task %s, old %ld, "
				  "new %d\n",
				  wrc_task_get_name(tid),
				  t->max_run_ticks, delta_ticks);
		}
		t->max_run_ticks = delta_ticks;
	}
	if (print_task_time_threshold
            && delta_ticks > print_task_time_threshold)
		pp_printf("task %s, run for %d ms\n",
			  wrc_task_get_name(tid), delta_ticks);

	prev_ticks_for_profile = ticks;
}

/* Run a task with profiling */
static void wrc_run_task(unsigned tid)
{
	const struct wrc_task *t = &tasks[tid];
	struct wrc_task_usage *u = &tasks_usage[tid];
	int done_sth = 0;

	if (!t->job) /* idle task, just count iterations */
		u->nrun++;
	else if (!t->enabled || t->enabled() ) {
		/* either enabled or without a check variable */
		done_sth = t->job();
		u->nrun += done_sth;
	}
	account_task(tid, done_sth);
}

struct wrc_task_usage *wrc_task_get_usage(int tid)
{
	assert (tid < WRC_NBR_TASKS, "invalid tid");
	return &tasks_usage[tid];
}

const char *wrc_task_get_name(int tid)
{
	return tasks[tid].name;
}

unsigned wrc_task_nbr(void)
{
	return WRC_NBR_TASKS;
}

void wrc_poll_all_tasks(void)
{
	int i;

	for( i = 0; i < WRC_NBR_TASKS; i++ )
		wrc_run_task(i);
}

void wrc_tasks_run_inits(void)
{
	int i;

	for( i = 0; i < WRC_NBR_TASKS; i++ )
		if(tasks[i].init)
			tasks[i].init();
}

void wrc_tasks_accounting_init(void)
{
	shw_pps_gen_get_time(NULL, &prev_nanos_for_profile);
	/* get tics */
	prev_ticks_for_profile = timer_get_tics();
}

int wrc_task_not_yet(uint32_t *lastt, unsigned period)
{
	uint32_t now = timer_get_tics();

	if (!*lastt) {
		*lastt = now;
		return 0;
	}
	if (time_before(now, *lastt + period))
		return 1; /* not yet */

	*lastt += period;
	return 0;
}
