#ifndef RK3576_PROCESS_SUPERVISOR_H
#define RK3576_PROCESS_SUPERVISOR_H

#include <sys/types.h>
#include <stdint.h>

#define SUP_MAX_TASKS 20

typedef struct {
    char name[64];
    char argv0[256];
    char arg1[256];
    char arg2[256];
    char arg3[256];
    char arg4[256];
    char arg5[256];
    char arg6[256];
    char arg7[256];
    char arg8[256];
    char arg9[256];
    char arg10[256];
    char arg11[256];
    pid_t pid;
    int exit_status;
    int exited;
    int64_t start_ms;
} SupTask;

typedef struct {
    SupTask tasks[SUP_MAX_TASKS];
    int count;
} SupState;

int sup_add(SupState *st, const char *name, const char *bin, const char *a1,
            const char *a2);
int sup_add4(SupState *st, const char *name, const char *bin, const char *a1,
             const char *a2, const char *a3, const char *a4);
int sup_add6(SupState *st, const char *name, const char *bin, const char *a1,
             const char *a2, const char *a3, const char *a4, const char *a5,
             const char *a6);
int sup_add9(SupState *st, const char *name, const char *bin, const char *a1,
             const char *a2, const char *a3, const char *a4, const char *a5,
             const char *a6, const char *a7, const char *a8, const char *a9);
int sup_add11(SupState *st, const char *name, const char *bin, const char *a1,
              const char *a2, const char *a3, const char *a4, const char *a5,
              const char *a6, const char *a7, const char *a8, const char *a9,
              const char *a10, const char *a11);
int sup_start_all(SupState *st);
int sup_start_one(SupState *st, int idx);
int sup_wait_any(SupState *st, pid_t *which_pid, int *status);
void sup_print_status(pid_t pid, int status);

#endif
