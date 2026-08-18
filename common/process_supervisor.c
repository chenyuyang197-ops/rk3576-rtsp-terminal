#include "process_supervisor.h"

#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/wait.h>

static int64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void sup_fill_task(SupTask *t, const char *name, const char *bin, const char *a1,
                          const char *a2, const char *a3, const char *a4, const char *a5,
                          const char *a6, const char *a7, const char *a8, const char *a9,
                          const char *a10, const char *a11) {
    snprintf(t->name, sizeof(t->name), "%s", name);
    snprintf(t->argv0, sizeof(t->argv0), "%s", bin);
    snprintf(t->arg1, sizeof(t->arg1), "%s", a1 ? a1 : "");
    snprintf(t->arg2, sizeof(t->arg2), "%s", a2 ? a2 : "");
    snprintf(t->arg3, sizeof(t->arg3), "%s", a3 ? a3 : "");
    snprintf(t->arg4, sizeof(t->arg4), "%s", a4 ? a4 : "");
    snprintf(t->arg5, sizeof(t->arg5), "%s", a5 ? a5 : "");
    snprintf(t->arg6, sizeof(t->arg6), "%s", a6 ? a6 : "");
    snprintf(t->arg7, sizeof(t->arg7), "%s", a7 ? a7 : "");
    snprintf(t->arg8, sizeof(t->arg8), "%s", a8 ? a8 : "");
    snprintf(t->arg9, sizeof(t->arg9), "%s", a9 ? a9 : "");
    snprintf(t->arg10, sizeof(t->arg10), "%s", a10 ? a10 : "");
    snprintf(t->arg11, sizeof(t->arg11), "%s", a11 ? a11 : "");
    t->pid = -1;
    t->exited = 0;
    t->start_ms = 0;
}

static int sup_exec_task(SupTask *t) {
    char *args[16];
    int n = 0;
    args[n++] = t->argv0;
    if (t->arg1[0]) {
        args[n++] = t->arg1;
    }
    if (t->arg2[0]) {
        args[n++] = t->arg2;
    }
    if (t->arg3[0]) {
        args[n++] = t->arg3;
    }
    if (t->arg4[0]) {
        args[n++] = t->arg4;
    }
    if (t->arg5[0]) {
        args[n++] = t->arg5;
    }
    if (t->arg6[0]) {
        args[n++] = t->arg6;
    }
    if (t->arg7[0]) {
        args[n++] = t->arg7;
    }
    if (t->arg8[0]) {
        args[n++] = t->arg8;
    }
    if (t->arg9[0]) {
        args[n++] = t->arg9;
    }
    if (t->arg10[0]) {
        args[n++] = t->arg10;
    }
    if (t->arg11[0]) {
        args[n++] = t->arg11;
    }
    args[n] = NULL;
    execv(t->argv0, args);
    LOGE("execv failed for %s: %s", t->argv0, strerror(errno));
    _exit(127);
}

int sup_add(SupState *st, const char *name, const char *bin, const char *a1,
            const char *a2) {
    return sup_add6(st, name, bin, a1, a2, NULL, NULL, NULL, NULL);
}

int sup_add4(SupState *st, const char *name, const char *bin, const char *a1,
             const char *a2, const char *a3, const char *a4) {
    return sup_add6(st, name, bin, a1, a2, a3, a4, NULL, NULL);
}

int sup_add6(SupState *st, const char *name, const char *bin, const char *a1,
             const char *a2, const char *a3, const char *a4, const char *a5,
             const char *a6) {
    return sup_add9(st, name, bin, a1, a2, a3, a4, a5, a6, NULL, NULL, NULL);
}

int sup_add9(SupState *st, const char *name, const char *bin, const char *a1,
             const char *a2, const char *a3, const char *a4, const char *a5,
             const char *a6, const char *a7, const char *a8, const char *a9) {
    return sup_add11(st, name, bin, a1, a2, a3, a4, a5, a6, a7, a8, a9, NULL, NULL);
}

int sup_add11(SupState *st, const char *name, const char *bin, const char *a1,
              const char *a2, const char *a3, const char *a4, const char *a5,
              const char *a6, const char *a7, const char *a8, const char *a9,
              const char *a10, const char *a11) {
    if (st->count >= SUP_MAX_TASKS) {
        return -1;
    }
    sup_fill_task(&st->tasks[st->count++], name, bin, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10,
                  a11);
    return 0;
}

int sup_start_one(SupState *st, int idx) {
    if (idx < 0 || idx >= st->count) {
        return -1;
    }
    SupTask *t = &st->tasks[idx];
    pid_t pid = fork();
    if (pid < 0) {
        LOGE("fork failed: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        sup_exec_task(t);
    }
    t->pid = pid;
    t->start_ms = now_ms();
    LOGI("started %s pid=%d cmd=%s %s %s %s %s %s %s %s %s %s %s %s", t->name, (int)pid, t->argv0,
         t->arg1, t->arg2, t->arg3, t->arg4, t->arg5, t->arg6, t->arg7, t->arg8, t->arg9,
         t->arg10, t->arg11);
    return 0;
}

int sup_start_all(SupState *st) {
    for (int i = 0; i < st->count; ++i) {
        if (sup_start_one(st, i) < 0) {
            return -1;
        }
    }
    return 0;
}

int sup_wait_any(SupState *st, pid_t *which_pid, int *status) {
    int stt;
    pid_t pid = waitpid(-1, &stt, 0);
    if (pid <= 0) {
        return -1;
    }
    for (int i = 0; i < st->count; ++i) {
        if (st->tasks[i].pid == pid) {
            st->tasks[i].exited = 1;
            st->tasks[i].exit_status = stt;
            *which_pid = pid;
            *status = stt;
            int64_t runtime_ms = now_ms() - st->tasks[i].start_ms;
            LOGI("task %s pid=%d runtime=%lldms exited", st->tasks[i].name, (int)pid,
                 (long long)runtime_ms);
            sup_print_status(pid, stt);
            return i;
        }
    }
    return -1;
}

void sup_print_status(pid_t pid, int status) {
    if (WIFEXITED(status)) {
        LOGI("pid=%d exit code=%d", (int)pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        LOGW("pid=%d signal=%d", (int)pid, WTERMSIG(status));
    }
}
