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

void userAppInit(void)
{
    if (R_SUCCEEDED(socketInitializeDefault()))
        s_nxlink_sock = nxlinkStdio();
}

/* Runs on every way out (exit, return from main, abort) before hbloader
 * unmaps the program: stop our threads first, or they wake up in unmapped code. */
static void prof_stop(void);
static void boost_stop(void);

void userAppExit(void)
{
    prof_stop();
    boost_stop();
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

/* CPU boost: the emulation needs 1785 MHz (the CPU clock of the system's own
 * boost mode) to hold full speed; stock is 1020. Raise it at launch so no
 * overclocking tool is needed, and put the original rate back on exit.
 * Docking, undocking and returning from the Home Menu reapply the stock
 * clocks, so a thread raises it again when it finds it lower. A clock set
 * higher by a tool such as sys-clk is left alone. MPMM_CPU_BOOST=0 turns it
 * off; MPMM_CPU_BOOST=<MHz> picks another rate. */
static pthread_t s_boost;
static int s_boost_running;
static volatile int s_boost_stop;
static u32 s_boost_hz;
static u32 s_boost_orig_hz;
static int s_boost_clkrst;
static ClkrstSession s_boost_session;

static Result boost_get(u32 *hz)
{
    return s_boost_clkrst ? clkrstGetClockRate(&s_boost_session, hz)
                          : pcvGetClockRate(PcvModule_CpuBus, hz);
}

static Result boost_set(u32 hz)
{
    return s_boost_clkrst ? clkrstSetClockRate(&s_boost_session, hz)
                          : pcvSetClockRate(PcvModule_CpuBus, hz);
}

static void *boost_thread_main(void *arg)
{
    (void)arg;
    int raises = 0, warned = 0;
    while (!s_boost_stop) {
        u32 hz = 0;
        if (R_SUCCEEDED(boost_get(&hz)) && hz < s_boost_hz) {
            boost_set(s_boost_hz);
            /* Docking or HOME resets the clock now and then; a tool that
             * keeps lowering it is fighting the boost. */
            if (++raises == 5 && !warned) {
                warned = 1;
                fprintf(stderr, "[switch] the CPU clock keeps dropping to %u MHz: "
                                "an overclocking tool (sys-clk?) is setting a lower "
                                "speed for this title; remove that setting\n",
                        hz / 1000000u);
            }
        }
        for (int i = 0; i < 10 && !s_boost_stop; ++i)
            svcSleepThread(100000000ull);
    }
    return NULL;
}

static void boost_start(void)
{
    const char *e = getenv("MPMM_CPU_BOOST");
    long mhz = e ? strtol(e, NULL, 10) : 1785;
    if (mhz <= 0)
        return;
    s_boost_hz = (u32)mhz * 1000000u;

    if (hosversionAtLeast(8, 0, 0)) {
        if (R_FAILED(clkrstInitialize()))
            return;
        if (R_FAILED(clkrstOpenSession(&s_boost_session, PcvModuleId_CpuBus, 3))) {
            clkrstExit();
            return;
        }
        s_boost_clkrst = 1;
    } else if (R_FAILED(pcvInitialize())) {
        return;
    }
    boost_get(&s_boost_orig_hz);
    Result rc = boost_set(s_boost_hz);
    u32 now = 0;
    boost_get(&now);
    fprintf(stderr, "[switch] CPU boost %u -> %u MHz (%s)\n", s_boost_orig_hz / 1000000u,
            now / 1000000u, R_SUCCEEDED(rc) ? "ok" : "failed");
    if (pthread_create(&s_boost, NULL, boost_thread_main, NULL) == 0)
        s_boost_running = 1;
}

static void boost_stop(void)
{
    if (!s_boost_hz)
        return;
    if (s_boost_running) {
        s_boost_stop = 1;
        pthread_join(s_boost, NULL);
        s_boost_running = 0;
    }
    if (s_boost_orig_hz)
        boost_set(s_boost_orig_hz);
    if (s_boost_clkrst) {
        clkrstCloseSession(&s_boost_session);
        clkrstExit();
        s_boost_clkrst = 0;
    } else {
        pcvExit();
    }
    s_boost_hz = 0;
}

int __real_main(int argc, char **argv);
int __wrap_main(int argc, char **argv);
static void prof_start(void);

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

    /* Emulation owns core 0 (see __wrap_pthread_create). */
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, 0, 0x1);

    /* Started from the Homebrew Menu there is no terminal: keep the log in
     * log.txt beside the .nro, and the previous run's as log-previous.txt. */
    if (s_nxlink_sock < 0) {
        char log_path[PATH_MAX], prev_path[PATH_MAX];
        snprintf(log_path, sizeof(log_path), "%s/log.txt", dir);
        snprintf(prev_path, sizeof(prev_path), "%s/log-previous.txt", dir);
        rename(log_path, prev_path);
        /* Both streams append: newlib seeks to the end before each write only
         * in "a" mode, so a "w" stdout would overwrite stderr's lines. */
        FILE *log_new = fopen(log_path, "w");
        if (log_new)
            fclose(log_new);
        if (freopen(log_path, "a", stdout))
            setvbuf(stdout, NULL, _IOLBF, 0);
        if (freopen(log_path, "a", stderr))
            setvbuf(stderr, NULL, _IOLBF, 0);
    }

    /* environment.txt beside the .nro: NAME=value per line ('#' comments),
     * the runtime's settings without a PC. nxlink arguments override it. */
    char env_path[PATH_MAX];
    snprintf(env_path, sizeof(env_path), "%s/environment.txt", dir);
    FILE *envf = fopen(env_path, "r");
    if (envf) {
        char line[512];
        while (fgets(line, sizeof(line), envf)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == '#' || *p == '\0' || *p == '\n' || *p == '\r')
                continue;
            p[strcspn(p, "\r\n")] = '\0';
            char *eq = strchr(p, '=');
            if (!eq || eq == p)
                continue;
            *eq = '\0';
            setenv(p, eq + 1, 1);
            fprintf(stderr, "[switch] environment.txt %s=%s\n", p, eq + 1);
        }
        fclose(envf);
    }

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

    /* Switch performance defaults (an nxlink NAME=value argument overrides):
     * batch DMA word events, present without blocking on vsync (the runtime's
     * wall-clock pacer keeps the speed), no Smooth motion redraws, and no
     * per-audio-sample scheduler deadline. Measured on hardware at 1785 MHz
     * (16:9): FMV 25 -> 43 Hz, gameplay 40 -> 47 Hz. */
    setenv("PSX_DMA_EVENT_WINDOW", "256", 0);
    setenv("PSX_VSYNC", "0", 0);
    setenv("PSX_FRAME_GEN", "0", 0);
    setenv("PSX_SPU_SAMPLE_EVENTS", "0", 0);

    fprintf(stderr, "[switch] exe %s\n", exe_path);
    boost_start();
    prof_start();
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
 * The main thread runs the whole emulation (CPU, devices, GPU command
 * processing) and is the bottleneck, so it gets core 0 to itself; every other
 * thread may run on cores 1-2, starting on 1, 2, 1, ... in turn. The render
 * thread spin-waits before it sleeps, and on core 0 that took emulation time.
 * libnx also gives threads a 128 KiB stack where glibc gives 8 MiB, and the
 * runtime's threads overflowed it (classic mode crashed with SP below the
 * stack region), so threads without their own stack get 8 MiB. */
#define SWITCH_THREAD_STACK (8u << 20)

typedef struct {
    void *(*start)(void *);
    void *arg;
    int core;
} thread_trampoline;

static void prof_register_thread(Handle h, void *start);

static void *thread_on_core(void *p)
{
    thread_trampoline t = *(thread_trampoline *)p;
    free(p);
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, t.core, 0x6);
    prof_register_thread(threadGetCurHandle(), (void *)t.start);
    return t.start(t.arg);
}

int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg);

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start)(void *), void *arg)
{
    static int next_core = 0;
    thread_trampoline *t = malloc(sizeof(*t));
    if (!t)
        return __real_pthread_create(thread, attr, start, arg);
    t->start = start;
    t->arg = arg;
    t->core = 1 + __atomic_fetch_add(&next_core, 1, __ATOMIC_RELAXED) % 2;
    pthread_attr_t big;
    if (attr)
        big = *attr;
    else
        pthread_attr_init(&big);
    size_t size = 0;
    pthread_attr_getstacksize(&big, &size);
    if (!big.stackaddr && size < SWITCH_THREAD_STACK)
        pthread_attr_setstacksize(&big, SWITCH_THREAD_STACK);
    int rc = __real_pthread_create(thread, &big, thread_on_core, t);
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

/* Sampling profiler (SWITCH_PROF=1): a high-priority thread pauses every
 * runtime thread about once a millisecond, reads its PC and LR, and resumes
 * it. Every 5 s it writes the window's histogram to
 * /switch/mspacman/prof/wNNN.txt as ELF offsets ("T idx start", then
 * "P idx pc n", "L idx lr n" and "S idx n pc lr ret..." call stacks);
 * symbolize on the PC with an nm table. */
#define PROF_MAX_THREADS 32
#define PROF_SLOTS (1u << 17)

typedef struct {
    uint64_t key;
    uint32_t count;
} prof_slot;

#define PROF_STACK_DEPTH 8
#define PROF_STACK_SLOTS (1u << 14)

typedef struct {
    uint64_t hash;
    uint32_t count;
    uint32_t thread;
    uint64_t frames[PROF_STACK_DEPTH];
} prof_stack;

static prof_stack *s_prof_stacks;
static Handle s_prof_handles[PROF_MAX_THREADS];
static uintptr_t s_prof_starts[PROF_MAX_THREADS];
static int s_prof_nthreads;
static int s_prof_on = -1;
static prof_slot *s_prof_hist;
static Thread s_prof_thread;
static volatile int s_prof_stop;

static int prof_enabled(void)
{
    if (s_prof_on < 0) {
        const char *e = getenv("SWITCH_PROF");
        s_prof_on = e && e[0] == '1';
    }
    return s_prof_on;
}

static void prof_register_thread(Handle h, void *start)
{
    if (!prof_enabled())
        return;
    int i = __atomic_fetch_add(&s_prof_nthreads, 1, __ATOMIC_RELAXED);
    if (i >= PROF_MAX_THREADS)
        return;
    s_prof_starts[i] = (uintptr_t)start;
    __atomic_store_n(&s_prof_handles[i], h, __ATOMIC_RELEASE);
}

static void prof_count(uint64_t key)
{
    uint32_t i = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 47) & (PROF_SLOTS - 1);
    for (uint32_t n = 0; n < PROF_SLOTS; ++n, i = (i + 1) & (PROF_SLOTS - 1)) {
        if (s_prof_hist[i].key == key) {
            s_prof_hist[i].count++;
            return;
        }
        if (s_prof_hist[i].count == 0) {
            s_prof_hist[i].key = key;
            s_prof_hist[i].count = 1;
            return;
        }
    }
}

/* Walk the paused thread's x29 chain (the runtime keeps frame pointers),
 * staying inside the mapping that holds its SP so a stray FP can't fault. */
static void prof_count_stack(int thread, const ThreadContext *ctx, uintptr_t base)
{
    MemoryInfo mi;
    u32 page;
    if (R_FAILED(svcQueryMemory(&mi, &page, ctx->sp)))
        return;
    uint64_t lo = ctx->sp, hi = mi.addr + mi.size;
    uint64_t frames[PROF_STACK_DEPTH] = { ctx->pc.x - base, ctx->lr - base };
    int n = 2;
    uint64_t fp = ctx->fp;
    while (n < PROF_STACK_DEPTH && fp >= lo && fp + 16 <= hi && (fp & 7) == 0) {
        const uint64_t *rec = (const uint64_t *)fp;
        frames[n++] = rec[1] - base;
        if (rec[0] <= fp)
            break;
        fp = rec[0];
    }
    uint64_t h = 0xcbf29ce484222325ull ^ (uint64_t)thread;
    for (int i = 0; i < n; ++i)
        h = (h ^ frames[i]) * 0x100000001b3ull;
    h |= 1;
    uint32_t i = (uint32_t)(h >> 50) & (PROF_STACK_SLOTS - 1);
    for (uint32_t k = 0; k < PROF_STACK_SLOTS; ++k, i = (i + 1) & (PROF_STACK_SLOTS - 1)) {
        prof_stack *e = &s_prof_stacks[i];
        if (e->hash == h) {
            e->count++;
            return;
        }
        if (e->hash == 0) {
            e->hash = h;
            e->count = 1;
            e->thread = (uint32_t)thread;
            memcpy(e->frames, frames, sizeof(frames));
            return;
        }
    }
}

static void prof_dump(int window, uint64_t samples)
{
    char path[64];
    snprintf(path, sizeof(path), "/switch/mspacman/prof/w%03d.txt", window);
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    uintptr_t base = (uintptr_t)__code_start;
    fprintf(f, "window %d samples %llu\n", window, (unsigned long long)samples);
    int nt = s_prof_nthreads < PROF_MAX_THREADS ? s_prof_nthreads : PROF_MAX_THREADS;
    for (int t = 0; t < nt; ++t)
        fprintf(f, "T %d 0x%lx\n", t,
                s_prof_starts[t] ? (unsigned long)(s_prof_starts[t] - base) : 0ul);
    for (uint32_t i = 0; i < PROF_SLOTS; ++i) {
        if (!s_prof_hist[i].count)
            continue;
        uint64_t k = s_prof_hist[i].key;
        fprintf(f, "%c %u 0x%lx %u\n", (k >> 63) ? 'L' : 'P',
                (unsigned)((k >> 56) & 0x7F), (unsigned long)(k & 0xFFFFFFFFFFFFFFull),
                s_prof_hist[i].count);
    }
    for (uint32_t i = 0; i < PROF_STACK_SLOTS; ++i) {
        const prof_stack *e = &s_prof_stacks[i];
        if (!e->hash)
            continue;
        fprintf(f, "S %u %u", e->thread, e->count);
        for (int k = 0; k < PROF_STACK_DEPTH && (k < 2 || e->frames[k]); ++k)
            fprintf(f, " 0x%lx", (unsigned long)e->frames[k]);
        fputc('\n', f);
    }
    fclose(f);
    memset(s_prof_hist, 0, PROF_SLOTS * sizeof(prof_slot));
    memset(s_prof_stacks, 0, PROF_STACK_SLOTS * sizeof(prof_stack));
}

static void prof_thread_main(void *arg)
{
    (void)arg;
    uintptr_t base = (uintptr_t)__code_start;
    uint64_t window_start = armGetSystemTick();
    uint64_t samples = 0;
    int window = 0;
    while (!s_prof_stop) {
        svcSleepThread(1000000ull);
        int nt = s_prof_nthreads < PROF_MAX_THREADS ? s_prof_nthreads : PROF_MAX_THREADS;
        for (int t = 0; t < nt; ++t) {
            Handle h = __atomic_load_n(&s_prof_handles[t], __ATOMIC_ACQUIRE);
            if (!h || R_FAILED(svcSetThreadActivity(h, ThreadActivity_Paused)))
                continue;
            ThreadContext ctx;
            Result rc = svcGetThreadContext3(&ctx, h);
            svcSetThreadActivity(h, ThreadActivity_Runnable);
            if (R_FAILED(rc))
                continue;
            uint64_t tag = (uint64_t)t << 56;
            prof_count(tag | ((ctx.pc.x - base) & 0xFFFFFFFFFFFFFFull));
            prof_count((1ull << 63) | tag | ((ctx.lr - base) & 0xFFFFFFFFFFFFFFull));
            prof_count_stack(t, &ctx, base);
            samples++;
        }
        if (armTicksToNs(armGetSystemTick() - window_start) >= 5000000000ull) {
            prof_dump(window++, samples);
            samples = 0;
            window_start = armGetSystemTick();
        }
    }
}

static void prof_start(void)
{
    if (!prof_enabled())
        return;
    s_prof_hist = calloc(PROF_SLOTS, sizeof(prof_slot));
    s_prof_stacks = calloc(PROF_STACK_SLOTS, sizeof(prof_stack));
    if (!s_prof_hist || !s_prof_stacks)
        return;
    mkdir("/switch/mspacman/prof", 0777);
    prof_register_thread(envGetMainThreadHandle(), (void *)__wrap_main);
    if (R_FAILED(threadCreate(&s_prof_thread, prof_thread_main, NULL, NULL,
                              0x10000, 0x20, -2)))
        return;
    svcSetThreadCoreMask(s_prof_thread.handle, 1, 0x6);
    if (R_SUCCEEDED(threadStart(&s_prof_thread)))
        fprintf(stderr, "[switch] sampling profiler on -> /switch/mspacman/prof/\n");
}

static void prof_stop(void)
{
    if (!s_prof_hist)
        return;
    s_prof_stop = 1;
    threadWaitForExit(&s_prof_thread);
    threadClose(&s_prof_thread);
}

