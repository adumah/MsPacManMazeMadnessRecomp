/* Nintendo Switch (Horizon, libnx + newlib) platform layer.
 *
 * Linked into the Switch build only (CMakeLists.txt, NINTENDO_SWITCH). The
 * PSXRecomp runtime itself stays POSIX; this file covers what Horizon does
 * differently, through libnx's weak init hooks and link-time --wrap:
 *
 * - logging: stdout/stderr go to `nxlink -s` on the PC when started that way;
 * - memory: started from the album, homebrew runs as an applet with a few
 *   hundred MiB, too little for the runtime; warn so the log says why;
 * - paths: argv[0] is "sdmc:/switch/mspacman/x.nro". std::filesystem on newlib
 *   sees no root directory in "sdmc:/..." and treats it as relative, so the
 *   runtime's exe-dir anchor would become "sdmc:/cwd/sdmc:/...". "/switch/..."
 *   is absolute to libstdc++ and newlib resolves it on sdmc:, the default device.
 *   getcwd() is wrapped the same way, since fs::absolute/current_path build on it;
 * - rename: the SD card's rename refuses to replace an existing file, and the
 *   runtime saves settings and mod state by writing aside and renaming over.
 */
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <switch.h>
#include <EGL/egl.h>
#include <GL/glcorearb.h>

static int s_nxlink_sock = -1;
static pthread_t s_heartbeat;
static int s_heartbeat_running;
static volatile int s_heartbeat_stop;

void userAppInit(void)
{
    if (R_SUCCEEDED(socketInitializeDefault()))
        s_nxlink_sock = nxlinkStdio();
}

/* Runs on every way out (exit, return from main, abort) before hbloader
 * unmaps the program: stop our thread first, or it wakes up in unmapped code. */
void userAppExit(void)
{
    if (s_heartbeat_running) {
        s_heartbeat_stop = 1;
        pthread_join(s_heartbeat, NULL);
    }
    if (s_nxlink_sock >= 0)
        close(s_nxlink_sock);
    socketExit();
    /* Returning would hand the process back to hbloader with the runtime's
     * other threads still parked in their waits, their stacks in the heap the
     * next NRO gets: the next launch then crashes in its first big malloc.
     * End the whole process instead (back to the Home Menu). */
    svcExitProcess();
}

/* The image base at run time, for turning logged addresses into ELF offsets
 * (the ELF is linked at 0). */
extern char __code_start[];

void __real_abort(void) __attribute__((noreturn));

void __wrap_abort(void)
{
    uintptr_t from = (uintptr_t)__builtin_return_address(0);
    fprintf(stderr, "[switch] abort() called from %p (ELF offset 0x%lx)\n",
            (void *)from, (unsigned long)(from - (uintptr_t)__code_start));
    __real_abort();
}

/* Startup heartbeat: every 3 s for the first 2 minutes, log the newest
 * completed launch phase (host_launch_timing.h), so a black screen on the
 * console tells us whether startup is busy, stuck in one phase, or dead. */
#include "host_launch_timing.h"

static void *startup_heartbeat(void *arg)
{
    (void)arg;
    uint64_t last_total = 0;
    for (int tick = 1; tick <= 40; ++tick) {
        for (int i = 0; i < 30; ++i) {
            if (s_heartbeat_stop)
                return NULL;
            svcSleepThread(100000000ull);
        }
        uint64_t total = 0;
        HostLaunchTimingEvent ev;
        uint32_t n = host_launch_timing_snapshot(&ev, 1, &total, NULL);
        if (n && total != last_total)
            fprintf(stderr, "[switch] t=%ds: %llu launch phases done, last %s%s%s (%llu ms, %s)\n",
                    tick * 3, (unsigned long long)total,
                    host_launch_timing_stage_name(ev.stage),
                    ev.package_id[0] ? " " : "", ev.package_id,
                    (unsigned long long)(ev.duration_us / 1000), ev.ok ? "ok" : "failed");
        else
            fprintf(stderr, "[switch] t=%ds: alive, %llu launch phases done\n",
                    tick * 3, (unsigned long long)total);
        last_total = total;
    }
    return NULL;
}

int __real_main(int argc, char **argv);

int __wrap_main(int argc, char **argv)
{
    static char exe_path[PATH_MAX];
    static char *fallback_argv[2];

    AppletType type = appletGetAppletType();
    if (type != AppletType_Application && type != AppletType_SystemApplication)
        fprintf(stderr, "[switch] running as an applet (album launch): memory is "
                        "limited; hold R while starting a game to launch in its place\n");

    /* argv[0] can be missing when started by a loader that doesn't pass it. */
    if (argc < 1 || !argv || !argv[0]) {
        fallback_argv[0] = "/switch/mspacman/mspacman.nro";
        fallback_argv[1] = NULL;
        argc = 1;
        argv = fallback_argv;
    }
    const char *a0 = argv[0];
    if (strncmp(a0, "sdmc:", 5) == 0)
        a0 += 5;
    snprintf(exe_path, sizeof(exe_path), "%s", a0);
    argv[0] = exe_path;

    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", exe_path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        chdir(dir);
    }

    /* Horizon has no /tmp, and std::filesystem::temp_directory_path() throws
     * when $TMPDIR (default /tmp) isn't a directory. Keep one in our folder. */
    char tmp_dir[PATH_MAX];
    snprintf(tmp_dir, sizeof(tmp_dir), "%s/tmp", dir);
    mkdir(tmp_dir, 0777);
    setenv("TMPDIR", tmp_dir, 1);

    /* The AOT overlays are linked in, and Horizon gives homebrew no executable
     * memory to load compiled overlay caches into anyway. */
    setenv("PSX_OVERLAY_AUTOCOMPILE_OFF", "1", 1);

    /* There's no shell to set the runtime's diagnostic variables from:
     * NAME=value arguments (nxlink nro NAME=value ...) go to the environment
     * instead of the runtime. Then boot straight into the game, as
     * launch-mspacman.sh does on Linux. */
    static char no_launcher[] = "--no-launcher";
    char **args = calloc((size_t)argc + 2, sizeof(*args));
    if (args) {
        int n = 0;
        args[n++] = argv[0];
        for (int i = 1; i < argc; ++i) {
            char *eq = strchr(argv[i], '=');
            if (argv[i][0] != '-' && eq && eq != argv[i]) {
                *eq = '\0';
                setenv(argv[i], eq + 1, 1);
                fprintf(stderr, "[switch] env %s=%s\n", argv[i], eq + 1);
                *eq = '=';
                continue;
            }
            args[n++] = argv[i];
        }
        args[n++] = no_launcher;
        args[n] = NULL;
        argc = n;
        argv = args;
    }

    fprintf(stderr, "[switch] exe %s\n", exe_path);
    if (pthread_create(&s_heartbeat, NULL, startup_heartbeat, NULL) == 0)
        s_heartbeat_running = 1;
    return __real_main(argc, argv);
}

/* GL: the renderer calls GL 1.x-3.x entry points directly. libGLESv2 exports
 * most of them through Mesa's shared dispatch (valid for our desktop core
 * context too); these two are desktop-only, so fetch them from EGL. */
typedef void (*gl_get_tex_image_fn)(GLenum, GLint, GLenum, GLenum, void *);
typedef void (*gl_clear_depth_fn)(GLdouble);

void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels)
{
    static gl_get_tex_image_fn fn;
    if (!fn) fn = (gl_get_tex_image_fn)eglGetProcAddress("glGetTexImage");
    if (fn) fn(target, level, format, type, pixels);
}

void glClearDepth(GLdouble depth)
{
    static gl_clear_depth_fn fn;
    if (!fn) fn = (gl_clear_depth_fn)eglGetProcAddress("glClearDepth");
    if (fn) fn(depth);
}

/* Processes, pipes and signals: Horizon has none. The runtime only reaches
 * them for self-relaunch, overlay autocompile and BIOS-module tooling, all
 * off on the Switch; fail cleanly if something gets there anyway. */
pid_t waitpid(pid_t pid, int *status, int options)
{ (void)pid; (void)status; (void)options; errno = ECHILD; return -1; }
int execv(const char *path, char *const argv[])
{ (void)path; (void)argv; errno = ENOSYS; return -1; }
int execl(const char *path, const char *arg, ...)
{ (void)path; (void)arg; errno = ENOSYS; return -1; }
int setpgid(pid_t pid, pid_t pgid)
{ (void)pid; (void)pgid; errno = ENOSYS; return -1; }
int pipe(int fds[2])
{ (void)fds; errno = ENOSYS; return -1; }
FILE *popen(const char *command, const char *mode)
{ (void)command; (void)mode; errno = ENOSYS; return NULL; }
int pclose(FILE *stream)
{ (void)stream; errno = ECHILD; return -1; }
int nice(int inc)
{ (void)inc; return 0; }
int setitimer(int which, const struct itimerval *value, struct itimerval *old)
{
    (void)which; (void)value;
    if (old) memset(old, 0, sizeof(*old));
    errno = ENOSYS;
    return -1;
}
int sigprocmask(int how, const sigset_t *set, sigset_t *old)
{
    (void)how; (void)set;
    if (old) memset(old, 0, sizeof(*old));
    return 0;
}
int timespec_get(struct timespec *ts, int base)
{
    if (base != TIME_UTC || clock_gettime(CLOCK_REALTIME, ts) != 0)
        return 0;
    return base;
}

/* Threads: libnx starts every pthread (and so every std::thread) on the
 * creating thread's core, and Horizon doesn't time-slice threads of equal
 * priority on one core. A busy or spin-waiting main thread then starves the
 * runtime's other threads (overlay init, render, CD, audio) and startup hangs.
 * Let each new thread run on any of the application's cores 0-2, starting on
 * 1, 2, 0, ... in turn; the main thread keeps core 0. */
typedef struct {
    void *(*start)(void *);
    void *arg;
    int core;
} thread_trampoline;

static void *thread_on_core(void *p)
{
    thread_trampoline t = *(thread_trampoline *)p;
    free(p);
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, t.core, 0x7);
    return t.start(t.arg);
}

int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg);

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg)
{
    static int next_core = 1;
    thread_trampoline *t = malloc(sizeof(*t));
    if (!t)
        return __real_pthread_create(thread, attr, start, arg);
    t->start = start;
    t->arg = arg;
    t->core = __atomic_fetch_add(&next_core, 1, __ATOMIC_RELAXED) % 3;
    int rc = __real_pthread_create(thread, attr, thread_on_core, t);
    if (rc != 0)
        free(t);
    return rc;
}

char *__real_getcwd(char *buf, size_t size);

/* libnx reports the cwd as "sdmc:/switch/mspacman"; drop the default device's
 * prefix so paths built on it stay absolute to std::filesystem. */
char *__wrap_getcwd(char *buf, size_t size)
{
    char *cwd = __real_getcwd(buf, size);
    if (cwd && strncmp(cwd, "sdmc:/", 6) == 0)
        memmove(cwd, cwd + 5, strlen(cwd + 5) + 1);
    return cwd;
}

int __real_rename(const char *from, const char *to);

int __wrap_rename(const char *from, const char *to)
{
    if (__real_rename(from, to) == 0)
        return 0;
    int saved = errno;
    if (access(to, F_OK) == 0 && unlink(to) == 0)
        return __real_rename(from, to);
    errno = saved;
    return -1;
}
