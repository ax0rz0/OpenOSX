#include <sys/types.h>
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <grp.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <xlocale.h>
#include <mach/vm_types.h>
#include <mach-o/loader.h>
#include <sys/qos.h>

extern int __pd_sys_pause(void) __asm("___pause");
extern pid_t __pd_sys_waitpid(pid_t pid, int *status, int options) __asm("___waitpid");
extern int _dyld_func_lookup(const char *name, void **address);
extern FILE *__pd_fdopen_extsn(int fd, const char *mode) __asm("_fdopen$DARWIN_EXTSN");
extern FILE *__pd_fopen_extsn(const char *path, const char *mode) __asm("_fopen$DARWIN_EXTSN");

extern int __pd_chmod_unix2003(const char *path, mode_t mode) __asm("_chmod$UNIX2003");
extern int __pd_fchmod_unix2003(int fd, mode_t mode) __asm("_fchmod$UNIX2003");
extern int __pd_fcntl_syscall(int fd, int cmd, long arg) __asm("___fcntl");
extern int __pd_getrlimit_unix2003(int resource, struct rlimit *rlp) __asm("_getrlimit$UNIX2003");
extern int __pd_kill_unix2003(pid_t pid, int sig) __asm("_kill$UNIX2003");
extern void *__pd_mmap_unix2003(void *addr, size_t len, int prot, int flags, int fd, off_t offset) __asm("_mmap$UNIX2003");
extern int __pd_mprotect_default(void *addr, size_t len, int prot) __asm("_mprotect");
extern int __pd_munmap_unix2003(void *addr, size_t len) __asm("_munmap$UNIX2003");
extern int __pd_open_unix2003(const char *path, int flags, mode_t mode) __asm("_open$UNIX2003");
extern int __pd_ptrace_syscall(int request, pid_t pid, caddr_t addr, int data) __asm("___ptrace");
extern int __pd_setrlimit_unix2003(int resource, const struct rlimit *rlp) __asm("_setrlimit$UNIX2003");
extern int __pd_sigsuspend_syscall(const sigset_t *set) __asm("___sigsuspend");
extern int __pd_nanosleep(const struct timespec *requested, struct timespec *remaining) __asm("_nanosleep");
extern ssize_t __pd_write_default_syscall(int fd, const void *buf, size_t nbyte) __asm("_write");

extern int __pd_pthread_cancel_unix2003(pthread_t thread) __asm("_pthread_cancel$UNIX2003");
extern int __pd_pthread_cond_init_unix2003(pthread_cond_t *cond, const pthread_condattr_t *attr) __asm("_pthread_cond_init$UNIX2003");
extern int __pd_pthread_cond_timedwait_unix2003(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime) __asm("_pthread_cond_timedwait$UNIX2003");
extern int __pd_pthread_cond_wait_unix2003(pthread_cond_t *cond, pthread_mutex_t *mutex) __asm("_pthread_cond_wait$UNIX2003");
extern int __pd_pthread_join_unix2003(pthread_t thread, void **value_ptr) __asm("_pthread_join$UNIX2003");
extern int __pd_pthread_mutexattr_destroy_unix2003(pthread_mutexattr_t *attr) __asm("_pthread_mutexattr_destroy$UNIX2003");
extern int __pd_pthread_rwlock_destroy_unix2003(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_destroy$UNIX2003");
extern int __pd_pthread_rwlock_init_unix2003(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr) __asm("_pthread_rwlock_init$UNIX2003");
extern int __pd_pthread_rwlock_rdlock_unix2003(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_rdlock$UNIX2003");
extern int __pd_pthread_rwlock_tryrdlock_unix2003(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_tryrdlock$UNIX2003");
extern int __pd_pthread_rwlock_trywrlock_unix2003(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_trywrlock$UNIX2003");
extern int __pd_pthread_rwlock_unlock_unix2003(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_unlock$UNIX2003");
extern int __pd_pthread_rwlock_wrlock_unix2003(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_wrlock$UNIX2003");
extern int __pd_pthread_setcancelstate_unix2003(int state, int *oldstate) __asm("_pthread_setcancelstate$UNIX2003");
extern int __pd_pthread_setcanceltype_unix2003(int type, int *oldtype) __asm("_pthread_setcanceltype$UNIX2003");
extern int __pd_pthread_sigmask_unix2003(int how, const sigset_t *set, sigset_t *oset) __asm("_pthread_sigmask$UNIX2003");
extern void __pd_pthread_testcancel_unix2003(void) __asm("_pthread_testcancel$UNIX2003");

int
getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
    uid_t real = getuid();
    uid_t effective = geteuid();

    if (ruid == NULL || euid == NULL || suid == NULL) {
        errno = EINVAL;
        return -1;
    }

    *ruid = real;
    *euid = effective;
    *suid = effective;
    return 0;
}

int
getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
    gid_t real = getgid();
    gid_t effective = getegid();

    if (rgid == NULL || egid == NULL || sgid == NULL) {
        errno = EINVAL;
        return -1;
    }

    *rgid = real;
    *egid = effective;
    *sgid = effective;
    return 0;
}

int
setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
    uid_t current_real = getuid();
    uid_t current_effective = geteuid();

    if (suid != (uid_t)-1 && suid != current_effective) {
        errno = ENOSYS;
        return -1;
    }
    if (ruid != (uid_t)-1 && ruid != current_real) {
        if (euid != (uid_t)-1 && euid != ruid) {
            errno = ENOSYS;
            return -1;
        }
        return setuid(ruid);
    }
    if (euid != (uid_t)-1 && euid != current_effective) {
        return seteuid(euid);
    }
    return 0;
}

int
setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
    gid_t current_real = getgid();
    gid_t current_effective = getegid();

    if (sgid != (gid_t)-1 && sgid != current_effective) {
        errno = ENOSYS;
        return -1;
    }
    if (rgid != (gid_t)-1 && rgid != current_real) {
        if (egid != (gid_t)-1 && egid != rgid) {
            errno = ENOSYS;
            return -1;
        }
        return setgid(rgid);
    }
    if (egid != (gid_t)-1 && egid != current_effective) {
        return setegid(egid);
    }
    return 0;
}

int
pause(void)
{
    return __pd_sys_pause();
}

pid_t
waitpid(pid_t pid, int *status, int options)
{
    return __pd_sys_waitpid(pid, status, options);
}

int
ptrace(int request, pid_t pid, caddr_t addr, int data)
{
    return __pd_ptrace_syscall(request, pid, addr, data);
}

int __pd_chmod_default(const char *path, mode_t mode) __asm("_chmod");
int
__pd_chmod_default(const char *path, mode_t mode)
{
    return __pd_chmod_unix2003(path, mode);
}

int __pd_fchmod_default(int fd, mode_t mode) __asm("_fchmod");
int
__pd_fchmod_default(int fd, mode_t mode)
{
    return __pd_fchmod_unix2003(fd, mode);
}

int __pd_fcntl_default(int fd, int cmd, ...) __asm("_fcntl");
int
__pd_fcntl_default(int fd, int cmd, ...)
{
    va_list ap;
    long arg;

    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);
    return __pd_fcntl_syscall(fd, cmd, arg);
}

int __pd_getrlimit_default(int resource, struct rlimit *rlp) __asm("_getrlimit");
int
__pd_getrlimit_default(int resource, struct rlimit *rlp)
{
    return __pd_getrlimit_unix2003(resource, rlp);
}

int __pd_kill_default(pid_t pid, int sig) __asm("_kill");
int
__pd_kill_default(pid_t pid, int sig)
{
    return __pd_kill_unix2003(pid, sig);
}

void *__pd_mmap_default(void *addr, size_t len, int prot, int flags, int fd, off_t offset) __asm("_mmap");
void *
__pd_mmap_default(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    return __pd_mmap_unix2003(addr, len, prot, flags, fd, offset);
}

int __pd_mprotect_unix2003(void *addr, size_t len, int prot) __asm("_mprotect$UNIX2003");
int
__pd_mprotect_unix2003(void *addr, size_t len, int prot)
{
    return __pd_mprotect_default(addr, len, prot);
}

int __pd_munmap_default(void *addr, size_t len) __asm("_munmap");
int
__pd_munmap_default(void *addr, size_t len)
{
    return __pd_munmap_unix2003(addr, len);
}

int __pd_open_default(const char *path, int flags, ...) __asm("_open");
int
__pd_open_default(const char *path, int flags, ...)
{
    va_list ap;
    mode_t mode = 0;

    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    return __pd_open_unix2003(path, flags, mode);
}

int __pd_setrlimit_default(int resource, const struct rlimit *rlp) __asm("_setrlimit");
int
__pd_setrlimit_default(int resource, const struct rlimit *rlp)
{
    return __pd_setrlimit_unix2003(resource, rlp);
}

int __pd_sigsuspend_default(const sigset_t *set) __asm("_sigsuspend");
int
__pd_sigsuspend_default(const sigset_t *set)
{
    return __pd_sigsuspend_syscall(set);
}

unsigned int __pd_sleep_default(unsigned int seconds) __asm("_sleep");
unsigned int
__pd_sleep_default(unsigned int seconds)
{
    struct timespec requested = { (time_t)seconds, 0 };
    struct timespec remaining = { 0, 0 };

    while (__pd_nanosleep(&requested, &remaining) == -1) {
        if (errno != EINTR) {
            return requested.tv_sec;
        }
        requested = remaining;
    }
    return 0;
}

unsigned int __pd_sleep_unix2003(unsigned int seconds) __asm("_sleep$UNIX2003");
unsigned int
__pd_sleep_unix2003(unsigned int seconds)
{
    return __pd_sleep_default(seconds);
}

ssize_t __pd_write_unix2003(int fd, const void *buf, size_t nbyte) __asm("_write$UNIX2003");
ssize_t
__pd_write_unix2003(int fd, const void *buf, size_t nbyte)
{
    return __pd_write_default_syscall(fd, buf, nbyte);
}

int __pd_pthread_cancel_default(pthread_t thread) __asm("_pthread_cancel");
int
__pd_pthread_cancel_default(pthread_t thread)
{
    return __pd_pthread_cancel_unix2003(thread);
}

int __pd_pthread_cond_init_default(pthread_cond_t *cond, const pthread_condattr_t *attr) __asm("_pthread_cond_init");
int
__pd_pthread_cond_init_default(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    return __pd_pthread_cond_init_unix2003(cond, attr);
}

int __pd_pthread_cond_timedwait_default(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime) __asm("_pthread_cond_timedwait");
int
__pd_pthread_cond_timedwait_default(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)
{
    return __pd_pthread_cond_timedwait_unix2003(cond, mutex, abstime);
}

int __pd_pthread_cond_wait_default(pthread_cond_t *cond, pthread_mutex_t *mutex) __asm("_pthread_cond_wait");
int
__pd_pthread_cond_wait_default(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    return __pd_pthread_cond_wait_unix2003(cond, mutex);
}

int __pd_pthread_join_default(pthread_t thread, void **value_ptr) __asm("_pthread_join");
int
__pd_pthread_join_default(pthread_t thread, void **value_ptr)
{
    return __pd_pthread_join_unix2003(thread, value_ptr);
}

int __pd_pthread_mutexattr_destroy_default(pthread_mutexattr_t *attr) __asm("_pthread_mutexattr_destroy");
int
__pd_pthread_mutexattr_destroy_default(pthread_mutexattr_t *attr)
{
    return __pd_pthread_mutexattr_destroy_unix2003(attr);
}

int __pd_pthread_rwlock_destroy_default(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_destroy");
int
__pd_pthread_rwlock_destroy_default(pthread_rwlock_t *rwlock)
{
    return __pd_pthread_rwlock_destroy_unix2003(rwlock);
}

int __pd_pthread_rwlock_init_default(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr) __asm("_pthread_rwlock_init");
int
__pd_pthread_rwlock_init_default(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr)
{
    return __pd_pthread_rwlock_init_unix2003(rwlock, attr);
}

int __pd_pthread_rwlock_rdlock_default(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_rdlock");
int
__pd_pthread_rwlock_rdlock_default(pthread_rwlock_t *rwlock)
{
    return __pd_pthread_rwlock_rdlock_unix2003(rwlock);
}

int __pd_pthread_rwlock_tryrdlock_default(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_tryrdlock");
int
__pd_pthread_rwlock_tryrdlock_default(pthread_rwlock_t *rwlock)
{
    return __pd_pthread_rwlock_tryrdlock_unix2003(rwlock);
}

int __pd_pthread_rwlock_trywrlock_default(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_trywrlock");
int
__pd_pthread_rwlock_trywrlock_default(pthread_rwlock_t *rwlock)
{
    return __pd_pthread_rwlock_trywrlock_unix2003(rwlock);
}

int __pd_pthread_rwlock_unlock_default(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_unlock");
int
__pd_pthread_rwlock_unlock_default(pthread_rwlock_t *rwlock)
{
    return __pd_pthread_rwlock_unlock_unix2003(rwlock);
}

int __pd_pthread_rwlock_wrlock_default(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_wrlock");
int
__pd_pthread_rwlock_wrlock_default(pthread_rwlock_t *rwlock)
{
    return __pd_pthread_rwlock_wrlock_unix2003(rwlock);
}

int __pd_pthread_setcancelstate_default(int state, int *oldstate) __asm("_pthread_setcancelstate");
int
__pd_pthread_setcancelstate_default(int state, int *oldstate)
{
    return __pd_pthread_setcancelstate_unix2003(state, oldstate);
}

int __pd_pthread_setcanceltype_default(int type, int *oldtype) __asm("_pthread_setcanceltype");
int
__pd_pthread_setcanceltype_default(int type, int *oldtype)
{
    return __pd_pthread_setcanceltype_unix2003(type, oldtype);
}

int __pd_pthread_sigmask_default(int how, const sigset_t *set, sigset_t *oset) __asm("_pthread_sigmask");
int
__pd_pthread_sigmask_default(int how, const sigset_t *set, sigset_t *oset)
{
    return __pd_pthread_sigmask_unix2003(how, set, oset);
}

void __pd_pthread_testcancel_default(void) __asm("_pthread_testcancel");
void
__pd_pthread_testcancel_default(void)
{
    __pd_pthread_testcancel_unix2003();
}

uint32_t
notify_check(int token, int *check)
{
    (void)token;
    if (check != NULL) {
        *check = 0;
    }
    return 0;
}

uint32_t
notify_register_check(const char *name, int *out_token)
{
    (void)name;
    if (out_token != NULL) {
        *out_token = 0;
    }
    return 0;
}

uint32_t
notify_monitor_file(int token, const char *path, int flags)
{
    (void)token;
    (void)path;
    (void)flags;
    return 0;
}

uint32_t
notify_cancel(int token)
{
    (void)token;
    return 0;
}

FILE *
__pd_fdopen_extsn(int fd, const char *mode)
{
    return fdopen(fd, mode);
}

FILE *
__pd_fopen_extsn(const char *path, const char *mode)
{
    return fopen(path, mode);
}

extern void __pd_syslog_extsn(int priority, const char *format, ...) __asm("_syslog$DARWIN_EXTSN");
extern void vsyslog(int priority, const char *format, va_list ap);

void
__pd_syslog_extsn(int priority, const char *format, ...)
{
    /* curl was compiled against the real SDK's syslog.h, which renames the
     * declaration to this mangled name via __DARWIN_ALIAS - our own
     * gen/oldsyslog.c syslog() is the plain (unrenamed) symbol, so just
     * forward. va_list can't cross a varargs call boundary portably, so
     * reimplement via vsyslog instead of calling syslog(priority, format...). */
    va_list ap;
    va_start(ap, format);
    vsyslog(priority, format, ap);
    va_end(ap);
}

size_t
fwrite(const void *ptr, size_t size, size_t count, FILE *stream)
{
    const unsigned char *bytes = ptr;
    size_t total = size * count;
    size_t written;

    if (size != 0 && count > ((size_t)-1 / size)) {
        errno = EINVAL;
        return 0;
    }

    for (written = 0; written < total; written++) {
        if (fputc(bytes[written], stream) == EOF) {
            break;
        }
    }

    return size == 0 ? count : written / size;
}

int
remove(const char *path)
{
    if (unlink(path) == 0) {
        return 0;
    }

    if (errno == EISDIR || errno == EPERM) {
        return rmdir(path);
    }

    return -1;
}

double
ldexp(double value, int exponent)
{
    return __builtin_ldexp(value, exponent);
}

int
dlclose(void *handle)
{
    typedef int (*dlclose_fn)(void *);
    static dlclose_fn fn;

    if (fn == NULL && !_dyld_func_lookup("__dyld_dlclose", (void **)&fn)) {
        return -1;
    }

    return fn(handle);
}

void *
dlopen(const char *path, int mode)
{
    typedef void *(*dlopen_fn)(const char *, int, void *);
    static dlopen_fn fn;

    if (fn == NULL && !_dyld_func_lookup("__dyld_dlopen_internal", (void **)&fn)) {
        return NULL;
    }

    return fn(path, mode, __builtin_return_address(0));
}

void *
dlsym(void *handle, const char *symbol)
{
    typedef void *(*dlsym_fn)(void *, const char *, void *);
    static dlsym_fn fn;

    if (fn == NULL && !_dyld_func_lookup("__dyld_dlsym_internal", (void **)&fn)) {
        return NULL;
    }

    return fn(handle, symbol, __builtin_return_address(0));
}

char *
dlerror(void)
{
    typedef char *(*dlerror_fn)(void);
    static dlerror_fn fn;

    if (fn == NULL && !_dyld_func_lookup("__dyld_dlerror", (void **)&fn)) {
        return NULL;
    }

    return fn();
}

long
atol(const char *str)
{
    return strtol(str, NULL, 10);
}

char *index(const char *s, int c)
{
    return __builtin_strchr(s, c);
}

/*
 * Minimal terminfo shims. On real Darwin these live in libncurses; PureDarwin
 * has neither an ncurses library nor a compiled terminfo database. xterm links
 * ncurses' setupterm()/tigetstr()/del_curterm()/cur_term for its *optional*
 * termcap-query and function-key feature (xtermcap.c, OPT_TCAP_*), and dyld
 * aborts at load if these flat-namespace symbols resolve nowhere. Provide just
 * enough to satisfy the linker and behave as "no terminal capabilities found":
 * setupterm succeeds so xterm's TcapInit passes, but every tigetstr() lookup
 * reports the capability absent, so no bogus escape sequences are invented.
 */
void *cur_term = NULL;

int
setupterm(const char *term, int filedes, int *errret)
{
    (void)term;
    (void)filedes;
    if (errret != NULL) {
        *errret = 1;            /* 1 == success, per the terminfo convention */
    }
    return 0;                   /* OK */
}

char *
tigetstr(const char *capname)
{
    (void)capname;
    return (char *)-1;          /* (char *)-1 == capability absent/cancelled */
}

int
del_curterm(void *oterm)
{
    (void)oterm;
    return 0;                   /* OK */
}

/* ncurses use_extended_names(): xterm calls it to toggle extended terminfo
 * capability names. With no terminfo backend there is nothing to toggle. */
int
use_extended_names(int enable)
{
    (void)enable;
    return 0;
}

/*
 * The rest of this file back-fills libc/libutil/libinfo functions that xterm
 * references but PureDarwin's static libc archives don't yet provide, so the
 * flat-namespace symbols it defers at link time actually resolve at load
 * instead of aborting dyld. Where a real implementation is cheap and correct
 * (alarm, the *_r passwd wrappers, openpty) we provide it; where the feature is
 * non-essential to xterm coming up (usershell iteration, reverse DNS) we
 * provide a minimal well-behaved stub.
 */

#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <netdb.h>
#include <pwd.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

unsigned int
alarm(unsigned int seconds)
{
    struct itimerval new_it;
    struct itimerval old_it;

    new_it.it_value.tv_sec = (time_t)seconds;
    new_it.it_value.tv_usec = 0;
    new_it.it_interval.tv_sec = 0;
    new_it.it_interval.tv_usec = 0;

    if (setitimer(ITIMER_REAL, &new_it, &old_it) < 0) {
        return 0;
    }

    /* Round any residual fraction up, as POSIX alarm() reports whole seconds. */
    return (unsigned int)old_it.it_value.tv_sec
        + (old_it.it_value.tv_usec != 0 ? 1u : 0u);
}


int
wcwidth(wchar_t wc)
{
    if (wc == 0) {
        return 0;
    }
    if (wc < 0x20 || (wc >= 0x7f && wc < 0xa0)) {
        return -1;              /* C0/C1 control characters */
    }
    /* No East-Asian-width tables here; treat everything printable as 1 cell.
     * xterm's own OPT_WIDE_CHARS logic refines this when enabled. */
    return 1;
}

/* Group-database iteration xterm touches on its setuid path (which PureDarwin
 * builds with --disable-setuid). Minimal, well-behaved stub. */
void
endgrent(void)
{
}

int
initgroups(const char *name, int basegid)
{
    (void)name;
    (void)basegid;
    return 0;
}

struct hostent *
gethostbyaddr(const void *addr, socklen_t len, int type)
{
    (void)addr;
    (void)len;
    (void)type;
    return NULL;                /* no reverse resolver; caller falls back */
}

char *
nl_langinfo(int item)
{
    /* CODESET is nl_langinfo item 0 on Darwin; report UTF-8 so xterm selects
     * its wide-character path. Everything else: empty string. */
    if (item == 0) {
        return (char *)"UTF-8";
    }
    return (char *)"";
}

static void
pd_pwcopy(struct passwd *dst, const struct passwd *src,
          char *buf, size_t buflen, struct passwd **result)
{
    size_t need_name = src->pw_name ? strlen(src->pw_name) + 1 : 0;
    size_t need_pass = src->pw_passwd ? strlen(src->pw_passwd) + 1 : 0;
    size_t need_gecos = src->pw_gecos ? strlen(src->pw_gecos) + 1 : 0;
    size_t need_dir = src->pw_dir ? strlen(src->pw_dir) + 1 : 0;
    size_t need_shell = src->pw_shell ? strlen(src->pw_shell) + 1 : 0;
    size_t off = 0;

    if (need_name + need_pass + need_gecos + need_dir + need_shell > buflen) {
        *result = NULL;         /* caller sees ERANGE via return value */
        return;
    }

    *dst = *src;

#define PD_PW_DUP(field, len)                                   \
    do {                                                        \
        if ((len) != 0) {                                       \
            memcpy(buf + off, src->field, (len));               \
            dst->field = buf + off;                             \
            off += (len);                                       \
        } else {                                                \
            dst->field = NULL;                                  \
        }                                                       \
    } while (0)

    PD_PW_DUP(pw_name, need_name);
    PD_PW_DUP(pw_passwd, need_pass);
    PD_PW_DUP(pw_gecos, need_gecos);
    PD_PW_DUP(pw_dir, need_dir);
    PD_PW_DUP(pw_shell, need_shell);
#undef PD_PW_DUP

    *result = dst;
}

int
getpwuid_r(uid_t uid, struct passwd *pwd, char *buffer, size_t bufsize,
           struct passwd **result)
{
    struct passwd *found = getpwuid(uid);

    if (found == NULL) {
        *result = NULL;
        return 0;               /* not found is not an error */
    }
    pd_pwcopy(pwd, found, buffer, bufsize, result);
    return (*result == NULL) ? ERANGE : 0;
}

int
getpwnam_r(const char *login, struct passwd *pwd, char *buffer, size_t bufsize,
           struct passwd **result)
{
    struct passwd *found = getpwnam(login);

    if (found == NULL) {
        *result = NULL;
        return 0;
    }
    pd_pwcopy(pwd, found, buffer, bufsize, result);
    return (*result == NULL) ? ERANGE : 0;
}

/*
 * PureDarwin's libc archive ships none of the Unix98 pty helpers, so provide
 * them here directly over /dev/ptmx using Darwin's pty ioctls (sys/ttycom.h).
 * These are the same operations Apple's libc performs; whether they succeed at
 * runtime depends on the kernel's ptmx/ptsd driver being present.
 */
#ifndef TIOCPTYGRANT
#define TIOCPTYGRANT _IO('t', 84)
#endif
#ifndef TIOCPTYGNAME
#define TIOCPTYGNAME _IOC(IOC_OUT, 't', 83, 128)
#endif
#ifndef TIOCPTYUNLK
#define TIOCPTYUNLK _IO('t', 82)
#endif

int
posix_openpt(int oflag)
{
    return open("/dev/ptmx", oflag);
}

int
grantpt(int fildes)
{
    return ioctl(fildes, TIOCPTYGRANT);
}

int
unlockpt(int fildes)
{
    return ioctl(fildes, TIOCPTYUNLK);
}

char *
ptsname(int fildes)
{
    static char buffer[128];

    if (ioctl(fildes, TIOCPTYGNAME, buffer) < 0) {
        return NULL;
    }
    return buffer;
}

int
openpty(int *amaster, int *aslave, char *name,
        struct termios *termp, struct winsize *winp)
{
    int master;
    int slave;
    char *slave_name;

    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        return -1;
    }
    if (grantpt(master) < 0 || unlockpt(master) < 0) {
        close(master);
        return -1;
    }
    slave_name = ptsname(master);
    if (slave_name == NULL) {
        close(master);
        return -1;
    }
    slave = open(slave_name, O_RDWR | O_NOCTTY);
    if (slave < 0) {
        close(master);
        return -1;
    }

    if (termp != NULL) {
        (void)tcsetattr(slave, TCSAFLUSH, termp);
    }
    if (winp != NULL) {
        (void)ioctl(slave, TIOCSWINSZ, winp);
    }
    if (name != NULL) {
        strcpy(name, slave_name);
    }

    *amaster = master;
    *aslave = slave;
    return 0;
}

/*
 * popen$DARWIN_EXTSN: xterm references the versioned symbol (the SDK's
 * <stdio.h> asm-renames popen). PureDarwin's libc archive exports neither
 * variant, so implement it here via fork/exec/pipe. Book-kept fds so the
 * companion pclose can reap; if pclose isn't linked the child is simply
 * reaped by the kernel at exit.
 */
extern FILE *__pd_popen_extsn(const char *command, const char *type)
    __asm("_popen$DARWIN_EXTSN");

FILE *
__pd_popen_extsn(const char *command, const char *type)
{
    int fds[2];
    int read_side;
    int want_read;
    pid_t pid;

    if (command == NULL || type == NULL
        || (type[0] != 'r' && type[0] != 'w')) {
        errno = EINVAL;
        return NULL;
    }
    want_read = (type[0] == 'r');

    if (pipe(fds) < 0) {
        return NULL;
    }

    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }

    if (pid == 0) {
        if (want_read) {
            dup2(fds[1], STDOUT_FILENO);
        } else {
            dup2(fds[0], STDIN_FILENO);
        }
        close(fds[0]);
        close(fds[1]);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }

    if (want_read) {
        close(fds[1]);
        read_side = fds[0];
    } else {
        close(fds[0]);
        read_side = fds[1];
    }
    return fdopen(read_side, want_read ? "r" : "w");
}

/*
 * select$1050: xterm's SDK <sys/select.h> asm-renames select to the 10.5
 * ("UNIX2003") variant. libSystem already provides select$DARWIN_EXTSN from
 * the libc archive, so forward the 1050 alias to it.
 */
extern int __pd_select_extsn(int nfds, fd_set *readfds, fd_set *writefds,
                             fd_set *errorfds, struct timeval *timeout)
    __asm("_select$DARWIN_EXTSN");
extern int __pd_select_1050(int nfds, fd_set *readfds, fd_set *writefds,
                            fd_set *errorfds, struct timeval *timeout)
    __asm("_select$1050");

int
__pd_select_1050(int nfds, fd_set *readfds, fd_set *writefds,
                 fd_set *errorfds, struct timeval *timeout)
{
    return __pd_select_extsn(nfds, readfds, writefds, errorfds, timeout);
}

/*
 * nano (via gnulib) needs these; none of them are in libc_static/
 * libsystem_kernel, so implement them directly.
 */

#include <dirent.h>
#include <search.h>
#include <wchar.h>

double
frexp(double value, int *exp)
{
    union { double d; uint64_t u; } v = { .d = value };
    uint64_t mant = v.u & 0x000fffffffffffffULL;
    int biased_exp = (int)((v.u >> 52) & 0x7ff);
    int sign = (v.u >> 63) & 1;

    if (biased_exp == 0x7ff || value == 0.0) {
        /* NaN, +-Inf, or +-0: return value unchanged, exponent 0. */
        *exp = 0;
        return value;
    }
    if (biased_exp == 0) {
        /* Subnormal: normalize by hand before extracting the exponent. */
        int shift = 0;
        while ((mant & 0x0010000000000000ULL) == 0) {
            mant <<= 1;
            shift++;
        }
        mant &= 0x000fffffffffffffULL;
        biased_exp = 1 - shift;
    }
    *exp = biased_exp - 1022;   /* bias 1023, plus one since mantissa gets a leading 0.5 */

    v.u = ((uint64_t)sign << 63) | (1022ULL << 52) | mant;   /* result in [0.5, 1.0) */
    return v.d;
}

/* The SDK's <signal.h> defines these as inlining macros; undef them so we
 * can provide real, linkable function symbols instead. */
#undef sigemptyset
#undef sigfillset
#undef sigaddset

int
sigemptyset(sigset_t *set)
{
    *set = 0;
    return 0;
}

int
sigfillset(sigset_t *set)
{
    *set = ~(sigset_t)0;
    return 0;
}

int
sigaddset(sigset_t *set, int signo)
{
    if (signo <= 0 || signo >= (int)(sizeof(sigset_t) * 8)) {
        errno = EINVAL;
        return -1;
    }
    *set |= ((sigset_t)1 << (signo - 1));
    return 0;
}

wchar_t *
wmemcpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
    return dst;
}

wchar_t *
wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (s[i] == c)
            return (wchar_t *)&s[i];
    return NULL;
}

wchar_t *
wcscat(wchar_t *dst, const wchar_t *src)
{
    wchar_t *d = dst;
    while (*d)
        d++;
    while ((*d++ = *src++))
        ;
    return dst;
}

/* This system's locale is fixed to UTF-8 (see nl_langinfo above), so the
 * wide<->multibyte conversions below implement UTF-8 directly rather than
 * consulting a locale table. mbsinit/btowc treat every byte state as
 * stateless (UTF-8 has no shift state), which is always safe to report. */

int
mbsinit(const mbstate_t *ps)
{
    (void)ps;
    return 1;
}

wint_t
btowc(int c)
{
    if (c == EOF)
        return WEOF;
    if ((unsigned char)c < 0x80)
        return (wint_t)(unsigned char)c;
    return WEOF;   /* multi-byte lead byte: not representable as a single wchar_t here */
}

static size_t
pd_wctoutf8(wchar_t wc, char *out)
{
    uint32_t c = (uint32_t)wc;

    if (c < 0x80) {
        if (out) out[0] = (char)c;
        return 1;
    } else if (c < 0x800) {
        if (out) {
            out[0] = (char)(0xC0 | (c >> 6));
            out[1] = (char)(0x80 | (c & 0x3F));
        }
        return 2;
    } else if (c < 0x10000) {
        if (out) {
            out[0] = (char)(0xE0 | (c >> 12));
            out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[2] = (char)(0x80 | (c & 0x3F));
        }
        return 3;
    } else {
        if (out) {
            out[0] = (char)(0xF0 | (c >> 18));
            out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
            out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[3] = (char)(0x80 | (c & 0x3F));
        }
        return 4;
    }
}

size_t
wcrtomb(char *s, wchar_t wc, mbstate_t *ps)
{
    (void)ps;
    if (s == NULL)
        return 1;   /* querying shift-state reset length: UTF-8 has none */
    return pd_wctoutf8(wc, s);
}

size_t
wcstombs(char *dst, const wchar_t *src, size_t n)
{
    size_t used = 0;

    while (*src) {
        char buf[4];
        size_t len = pd_wctoutf8(*src, buf);

        if (dst) {
            if (used + len > n)
                break;
            memcpy(dst + used, buf, len);
        } else if (used + len > n) {
            /* no destination: caller (wcsrtombs below) wants a length only */
        }
        used += len;
        src++;
    }
    if (dst && used < n)
        dst[used] = '\0';
    return used;
}

size_t
wcsrtombs(char *dst, const wchar_t **src, size_t n, mbstate_t *ps)
{
    (void)ps;
    size_t total = wcstombs(dst, *src, n);
    if (dst)
        *src = NULL;   /* whole string consumed (no embedded NUL support needed here) */
    return total;
}

wint_t
wctob(wint_t c)
{
    if (c == WEOF)
        return EOF;
    if ((uint32_t)c < 0x80)
        return (wint_t)(unsigned char)c;
    return WEOF;
}

/*
 * rewinddir$INODE64: opendir$INODE64/readdir$INODE64 (from libc_static) read
 * directory entries directly off the fd with no additional userspace
 * buffering layer here, so resetting the fd's file offset is equivalent to
 * reopening the stream at the start.
 */
void
__pd_rewinddir_inode64(DIR *dirp) __asm("_rewinddir$INODE64");

void
__pd_rewinddir_inode64(DIR *dirp)
{
    lseek(dirfd(dirp), 0, SEEK_SET);
}

/*
 * tsearch/tfind/tdelete/twalk: minimal unbalanced BST, sufficient for nano's
 * small in-memory lookup tables (it does not need tree-height guarantees).
 */
/* `key` must be the first member: callers dereference tsearch()'s return
 * value as `*(void **)node` to read the key back (the POSIX/glibc tsearch
 * calling convention), so the node pointer and the key-pointer slot must
 * coincide. */
struct pd_tnode {
    void            *key;
    struct pd_tnode *left;
    struct pd_tnode *right;
};

void *
tsearch(const void *key, void **rootp, int (*compar)(const void *, const void *))
{
    struct pd_tnode **cur = (struct pd_tnode **)rootp;

    if (rootp == NULL)
        return NULL;

    while (*cur != NULL) {
        int cmp = compar(key, (*cur)->key);
        if (cmp == 0)
            return *cur;
        cur = (cmp < 0) ? &(*cur)->left : &(*cur)->right;
    }

    {
        struct pd_tnode *n = (struct pd_tnode *)malloc(sizeof(*n));
        if (n == NULL)
            return NULL;
        n->left = n->right = NULL;
        n->key = (void *)key;
        *cur = n;
        return n;
    }
}

void *
tfind(const void *key, void *const *rootp, int (*compar)(const void *, const void *))
{
    struct pd_tnode *cur;

    if (rootp == NULL)
        return NULL;
    cur = *(struct pd_tnode *const *)rootp;
    while (cur != NULL) {
        int cmp = compar(key, cur->key);
        if (cmp == 0)
            return cur;
        cur = (cmp < 0) ? cur->left : cur->right;
    }
    return NULL;
}

/* Find the in-order successor's parent link to splice out `node`. */
static struct pd_tnode **
pd_tnode_min_link(struct pd_tnode **linkp)
{
    while ((*linkp)->left != NULL)
        linkp = &(*linkp)->left;
    return linkp;
}

void *
tdelete(const void *key, void **rootp, int (*compar)(const void *, const void *))
{
    struct pd_tnode **cur = (struct pd_tnode **)rootp;
    struct pd_tnode *parent_ret;

    if (rootp == NULL)
        return NULL;

    while (*cur != NULL) {
        int cmp = compar(key, (*cur)->key);
        if (cmp == 0)
            break;
        cur = (cmp < 0) ? &(*cur)->left : &(*cur)->right;
    }
    if (*cur == NULL)
        return NULL;

    parent_ret = (*cur == *(struct pd_tnode **)rootp) ? *cur : NULL;
    {
        struct pd_tnode *node = *cur;

        if (node->left == NULL) {
            *cur = node->right;
        } else if (node->right == NULL) {
            *cur = node->left;
        } else {
            struct pd_tnode **succ_link = pd_tnode_min_link(&node->right);
            struct pd_tnode *succ = *succ_link;

            *succ_link = succ->right;
            succ->left = node->left;
            succ->right = node->right;
            *cur = succ;
        }
        free(node);
    }
    return parent_ret ? *rootp : rootp;   /* glibc: non-NULL that isn't `node` itself */
}

static void
pd_twalk_r(const struct pd_tnode *node, void (*action)(const void *, VISIT, int), int depth)
{
    if (node == NULL)
        return;
    if (node->left == NULL && node->right == NULL) {
        action(node, leaf, depth);
        return;
    }
    action(node, preorder, depth);
    pd_twalk_r(node->left, action, depth + 1);
    action(node, postorder, depth);
    pd_twalk_r(node->right, action, depth + 1);
    action(node, endorder, depth);
}

void
twalk(const void *root, void (*action)(const void *, VISIT, int))
{
    pd_twalk_r((const struct pd_tnode *)root, action, 0);
}

int
dprintf(int fd, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return n;
    if ((size_t)n >= sizeof(buf))
        n = sizeof(buf) - 1;   /* truncate: matches a fixed-size fallback, not glibc's realloc growth */
    return (int)write(fd, buf, (size_t)n);
}

int
system(const char *command)
{
    pid_t pid;
    int status;

    if (command == NULL)
        return 1;   /* a shell is always "available" here */

    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return status;
}

ssize_t
getdelim(char **lineptr, size_t *n, int delim, FILE *stream)
{
    size_t cap, len = 0;
    char *buf;
    int c;

    if (lineptr == NULL || n == NULL || stream == NULL) {
        errno = EINVAL;
        return -1;
    }
    cap = (*lineptr != NULL && *n > 0) ? *n : 128;
    buf = (*lineptr != NULL) ? *lineptr : (char *)malloc(cap);
    if (buf == NULL)
        return -1;

    while ((c = fgetc(stream)) != EOF) {
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *nbuf = (char *)realloc(buf, ncap);
            if (nbuf == NULL) {
                *lineptr = buf;
                *n = cap;
                return -1;
            }
            buf = nbuf;
            cap = ncap;
        }
        buf[len++] = (char)c;
        if (c == delim)
            break;
    }
    if (len == 0 && c == EOF) {
        *lineptr = buf;
        *n = cap;
        return -1;
    }
    buf[len] = '\0';
    *lineptr = buf;
    *n = cap;
    return (ssize_t)len;
}

ssize_t
getline(char **lineptr, size_t *n, FILE *stream)
{
    return getdelim(lineptr, n, '\n', stream);
}

static void
pd_grcopy(struct group *dst, const struct group *src,
          char *buf, size_t buflen, struct group **result)
{
    size_t need_name = src->gr_name ? strlen(src->gr_name) + 1 : 0;
    size_t need_passwd = src->gr_passwd ? strlen(src->gr_passwd) + 1 : 0;
    size_t nmembers = 0, need_members, need_member_strings = 0, off = 0;
    size_t i;

    if (src->gr_mem != NULL) {
        while (src->gr_mem[nmembers] != NULL) {
            need_member_strings += strlen(src->gr_mem[nmembers]) + 1;
            nmembers++;
        }
    }
    need_members = (nmembers + 1) * sizeof(char *);

    if (need_name + need_passwd + need_members + need_member_strings > buflen) {
        *result = NULL;
        return;
    }

    *dst = *src;

    if (need_name != 0) {
        memcpy(buf + off, src->gr_name, need_name);
        dst->gr_name = buf + off;
        off += need_name;
    } else {
        dst->gr_name = NULL;
    }
    if (need_passwd != 0) {
        memcpy(buf + off, src->gr_passwd, need_passwd);
        dst->gr_passwd = buf + off;
        off += need_passwd;
    } else {
        dst->gr_passwd = NULL;
    }
    dst->gr_mem = (char **)(buf + off);
    off += need_members;
    for (i = 0; i < nmembers; i++) {
        size_t len = strlen(src->gr_mem[i]) + 1;
        memcpy(buf + off, src->gr_mem[i], len);
        dst->gr_mem[i] = buf + off;
        off += len;
    }
    dst->gr_mem[nmembers] = NULL;

    *result = dst;
}

int
getgrnam_r(const char *name, struct group *grp, char *buffer, size_t bufsize,
           struct group **result)
{
    struct group *found = getgrnam(name);

    if (found == NULL) {
        *result = NULL;
        return 0;
    }
    pd_grcopy(grp, found, buffer, bufsize, result);
    return (*result == NULL) ? ERANGE : 0;
}

int
getgrgid_r(gid_t gid, struct group *grp, char *buffer, size_t bufsize,
           struct group **result)
{
    struct group *found = getgrgid(gid);

    if (found == NULL) {
        *result = NULL;
        return 0;
    }
    pd_grcopy(grp, found, buffer, bufsize, result);
    return (*result == NULL) ? ERANGE : 0;
}

extern int __pd_getgroups(int gidsetsize, gid_t grouplist[]) __asm("_getgroups");
int __pd_getgroups_extsn(int gidsetsize, gid_t grouplist[]) __asm("_getgroups$DARWIN_EXTSN");

int
__pd_getgroups_extsn(int gidsetsize, gid_t grouplist[])
{
    return __pd_getgroups(gidsetsize, grouplist);
}

int
statvfs(const char *path, struct statvfs *buf)
{
    struct statfs sf;

    if (statfs(path, &sf) != 0)
        return -1;
    memset(buf, 0, sizeof(*buf));
    buf->f_bsize   = sf.f_bsize;
    buf->f_frsize  = sf.f_bsize;
    buf->f_blocks  = sf.f_blocks;
    buf->f_bfree   = sf.f_bfree;
    buf->f_bavail  = sf.f_bavail;
    buf->f_files   = sf.f_files;
    buf->f_ffree   = sf.f_ffree;
    buf->f_favail  = sf.f_ffree;
    buf->f_fsid    = (unsigned long)sf.f_fsid.val[0];
    buf->f_flag    = 0;
    buf->f_namemax = 255;
    return 0;
}


static const char *
pd_strptime_num(const char *s, int mindigits, int maxdigits, int *out)
{
    int n = 0, count = 0;

    while (count < maxdigits && *s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
        count++;
    }
    if (count < mindigits)
        return NULL;
    *out = n;
    return s;
}

char *
strptime(const char *s, const char *format, struct tm *tm)
{
    int val;

    while (*format) {
        if (*format == '%' && format[1]) {
            format++;
            switch (*format) {
            case 'Y':
                s = pd_strptime_num(s, 1, 4, &val);
                if (!s) return NULL;
                tm->tm_year = val - 1900;
                break;
            case 'y':
                s = pd_strptime_num(s, 1, 2, &val);
                if (!s) return NULL;
                tm->tm_year = (val < 69) ? val + 100 : val;
                break;
            case 'm':
                s = pd_strptime_num(s, 1, 2, &val);
                if (!s) return NULL;
                tm->tm_mon = val - 1;
                break;
            case 'd':
                s = pd_strptime_num(s, 1, 2, &val);
                if (!s) return NULL;
                tm->tm_mday = val;
                break;
            case 'H':
                s = pd_strptime_num(s, 1, 2, &val);
                if (!s) return NULL;
                tm->tm_hour = val;
                break;
            case 'M':
                s = pd_strptime_num(s, 1, 2, &val);
                if (!s) return NULL;
                tm->tm_min = val;
                break;
            case 'S':
                s = pd_strptime_num(s, 1, 2, &val);
                if (!s) return NULL;
                tm->tm_sec = val;
                break;
            case '%':
                if (*s != '%') return NULL;
                s++;
                break;
            case 'n':
            case 't':
                while (*s == ' ' || *s == '\t' || *s == '\n')
                    s++;
                break;
            default:
                return NULL;   /* unsupported specifier */
            }
            format++;
        } else if (*format == ' ') {
            while (*s == ' ' || *s == '\t')
                s++;
            format++;
        } else {
            if (*s != *format)
                return NULL;
            s++;
            format++;
        }
    }
    return (char *)s;
}

int
pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void))
{
    (void)prepare; (void)parent; (void)child;
    return 0;
}

/*
 * issetugid(): real Apple semantics report whether the process is running
 * with elevated privilege from a setuid/setgid exec (libc/CF/etc use it to
 * decide whether to trust environment variables). PureDarwin doesn't run
 * setuid binaries, so this is never true here - always report "no".
 */
int
issetugid(void)
{
    return 0;
}

typedef unsigned int sysdir_search_path_enumeration_state;

sysdir_search_path_enumeration_state
sysdir_start_search_path_enumeration(int dir, int domainMask)
{
    (void)dir; (void)domainMask;
    return 0;
}

sysdir_search_path_enumeration_state
sysdir_get_next_search_path_enumeration(sysdir_search_path_enumeration_state state, char *path)
{
    (void)state; (void)path;
    return 0;
}

vm_size_t vm_page_size = 4096;

int
OSAtomicCompareAndSwapPtrBarrier(void *oldValue, void *newValue, void * volatile *theValue)
{
    return __sync_bool_compare_and_swap(theValue, oldValue, newValue);
}

/*
 * _os_log_create: os_log_create(subsystem, category) is a macro
 * (os/log.h) expanding to _os_log_create(&__dso_handle, subsystem,
 * category) - the real ABI symbol callers actually link against. os_log
 * itself needs a real logging daemon (logd) this OS doesn't have; return a
 * distinct non-NULL handle so callers that just check "did creation
 * succeed" work, and route actual logging through os_log_type_enabled
 * always reporting "disabled" (real Apple's own default-safe fallback
 * shape) rather than implementing a full os_log pipeline.
 */
void *
_os_log_create(void *dso, const char *subsystem, const char *category)
{
    (void)dso; (void)subsystem; (void)category;
    static int dummy_log_handle;
    return &dummy_log_handle;
}

typedef unsigned int __pd_mvr_kern_return_t;
__pd_mvr_kern_return_t
mach_vm_region(unsigned int target_task, unsigned long long *address,
    unsigned long long *size, unsigned int flavor,
    int *info, unsigned int *infoCnt, unsigned int *object_name)
{
    (void)target_task; (void)address; (void)size; (void)flavor;
    (void)info; (void)infoCnt; (void)object_name;
    return 5; /* KERN_FAILURE */
}

int
pthread_getugid_np(uid_t *uid, gid_t *gid)
{
    if (uid) *uid = getuid();
    if (gid) *gid = getgid();
    return 0;
}

int
fprintf_l(FILE *stream, locale_t loc, const char *format, ...)
{
    (void)loc;
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}

extern int readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result);
extern int __pd_readdir_r_inode64(DIR *dirp, struct dirent *entry, struct dirent **result) __asm("_readdir_r$INODE64");
int
__pd_readdir_r_inode64(DIR *dirp, struct dirent *entry, struct dirent **result)
{
    return readdir_r(dirp, entry, result);
}

/*
 * asl(3) client API: fully __API_DEPRECATED in favor of os_log even on
 * real macOS (see asl.h) - CF only calls it as a last-resort logging
 * fallback. No asl daemon exists here either way, so this is a genuine
 * no-op client: asl_open/asl_new return a distinct non-NULL handle (so
 * callers don't treat creation itself as a failure), every other call
 * quietly succeeds without producing any actual log output.
 */
typedef struct __pd_asl_object_s *pd_asl_object_t;
static int pd_asl_dummy_object;

pd_asl_object_t
asl_open(const char *ident, const char *facility, uint32_t opts)
{
    (void)ident; (void)facility; (void)opts;
    return (pd_asl_object_t)&pd_asl_dummy_object;
}

pd_asl_object_t
asl_new(uint32_t type)
{
    (void)type;
    return (pd_asl_object_t)&pd_asl_dummy_object;
}

int
asl_set(pd_asl_object_t obj, const char *key, const char *value)
{
    (void)obj; (void)key; (void)value;
    return 0;
}

int
asl_send(pd_asl_object_t obj, pd_asl_object_t msg)
{
    (void)obj; (void)msg;
    return 0;
}

void
asl_free(pd_asl_object_t obj)
{
    (void)obj;
}

void
asl_close(pd_asl_object_t obj)
{
    (void)obj;
}

/*
 * Legacy OSAtomic ops (libkern/OSAtomic.h, deprecated since 10.12 in favor
 * of <stdatomic.h> but still linked by CF and others). Real implementations
 * via the same compiler intrinsics <stdatomic.h> itself would use.
 */
int32_t
OSAtomicIncrement32(volatile int32_t *theValue)
{
    return __sync_add_and_fetch(theValue, 1);
}

int32_t
OSAtomicDecrement32(volatile int32_t *theValue)
{
    return __sync_sub_and_fetch(theValue, 1);
}

int
OSAtomicCompareAndSwap32Barrier(int32_t oldValue, int32_t newValue, volatile int32_t *theValue)
{
    return __sync_bool_compare_and_swap(theValue, oldValue, newValue);
}

void
OSMemoryBarrier(void)
{
    __sync_synchronize();
}

/*
 * OSSpinLock: fully removed from the real SDK headers by 10.12 (superseded
 * by os_unfair_lock), but still linked here - implement both it and
 * os_unfair_lock with the same simple CAS spin loop. Not "unfair" in the
 * scheduler-hint sense real os_unfair_lock is, but correct as a mutual
 * exclusion primitive, which is all callers actually depend on.
 */
typedef volatile int32_t OSSpinLock;

void
OSSpinLockLock(OSSpinLock *lock)
{
    while (!__sync_bool_compare_and_swap(lock, 0, 1)) {
        /* spin */
    }
}

void
OSSpinLockUnlock(OSSpinLock *lock)
{
    __sync_lock_release(lock);
}

typedef struct { volatile int32_t locked; } pd_os_unfair_lock_s;

void
os_unfair_lock_lock(pd_os_unfair_lock_s *lock)
{
    while (!__sync_bool_compare_and_swap(&lock->locked, 0, 1)) {
        /* spin */
    }
}

void
os_unfair_lock_unlock(pd_os_unfair_lock_s *lock)
{
    __sync_lock_release(&lock->locked);
}

/*
 * os_log_type_enabled/_os_log_debug_impl/_os_log_error_impl: companions to
 * _os_log_create above. Every os_log_* call site is guarded by
 * `if (os_log_type_enabled(log, type))` before calling the _impl function,
 * so always reporting "disabled" means the _impl functions are linked
 * (satisfying the symbol reference) but never actually invoked at runtime.
 */
int
os_log_type_enabled(void *oslog, unsigned int type)
{
    (void)oslog; (void)type;
    return 0;
}

void
_os_log_debug_impl(void *dso, void *log, unsigned int type, const char *format, uint8_t *buf, uint32_t size)
{
    (void)dso; (void)log; (void)type; (void)format; (void)buf; (void)size;
}

void
_os_log_error_impl(void *dso, void *log, unsigned int type, const char *format, uint8_t *buf, uint32_t size)
{
    (void)dso; (void)log; (void)type; (void)format; (void)buf; (void)size;
}

/*
 * flsl: find-last-set-bit, POSIX-ish BSD extension (bit position of the
 * highest set bit, 1-based, 0 for input 0). Real implementation via the
 * same clz-based technique glibc's own equivalents use.
 */
int
flsl(long mask)
{
    return mask == 0 ? 0 : (int)(sizeof(long) * 8 - (unsigned)__builtin_clzl((unsigned long)mask));
}

/*
 * *_l (locale-variant) wrappers: no real per-thread locale support here
 * (every locale_t is effectively "C"/"POSIX"), so these just ignore the
 * locale_t argument and call the ordinary, already-real implementation.
 */
unsigned long
strtoul_l(const char *nptr, char **endptr, int base, locale_t loc)
{
    (void)loc;
    return strtoul(nptr, endptr, base);
}

long
strtol_l(const char *nptr, char **endptr, int base, locale_t loc)
{
    (void)loc;
    return strtol(nptr, endptr, base);
}

long long
strtoll_l(const char *nptr, char **endptr, int base, locale_t loc)
{
    (void)loc;
    return strtoll(nptr, endptr, base);
}

unsigned long long
strtoull_l(const char *nptr, char **endptr, int base, locale_t loc)
{
    (void)loc;
    return strtoull(nptr, endptr, base);
}

double
strtod_l(const char *nptr, char **endptr, locale_t loc)
{
    (void)loc;
    return strtod(nptr, endptr);
}

int
strncasecmp_l(const char *s1, const char *s2, size_t n, locale_t loc)
{
    (void)loc;
    return strncasecmp(s1, s2, n);
}

int
snprintf_l(char *str, size_t size, locale_t loc, const char *format, ...)
{
    (void)loc;
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

/*
 * uuid_generate_random/uuid_generate_time: real implementations, backed by
 * arc4random (already real elsewhere in this tree) rather than actually
 * implementing the RFC 4122 time-based variant for _time - a random UUID
 * is a safe, always-valid substitute (still unique, just not derived from
 * a clock/node-id the way the real v1 algorithm would).
 */
extern void arc4random_buf(void *buf, size_t nbytes);

static void
pd_uuid_generate_v4(unsigned char out[16])
{
    arc4random_buf(out, 16);
    out[6] = (out[6] & 0x0F) | 0x40; /* version 4 */
    out[8] = (out[8] & 0x3F) | 0x80; /* variant 10xx */
}

void
uuid_generate_random(unsigned char out[16])
{
    pd_uuid_generate_v4(out);
}

void
uuid_generate_time(unsigned char out[16])
{
    pd_uuid_generate_v4(out);
}

/*
 * getsectbynamefromheader_64: real mach-o section lookup by segment/section
 * name, used by CF's own __CFGetSectDataPtr for locating its Unicode data
 * sections. Real implementation - just walks the load commands.
 */
const struct section_64 *
getsectbynamefromheader_64(const struct mach_header_64 *mhp, const char *segname, const char *sectname)
{
    const struct load_command *lc = (const struct load_command *)((const char *)mhp + sizeof(struct mach_header_64));
    for (uint32_t i = 0; i < mhp->ncmds; i++) {
        if (lc->cmd == 0x19 /* LC_SEGMENT_64 */) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            if (strncmp(sg->segname, segname, 16) == 0) {
                const struct section_64 *sect = (const struct section_64 *)((const char *)sg + sizeof(struct segment_command_64));
                for (uint32_t j = 0; j < sg->nsects; j++) {
                    if (strncmp(sect[j].sectname, sectname, 16) == 0)
                        return &sect[j];
                }
            }
        }
        lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
    }
    return NULL;
}

unsigned int
mk_timer_create(void)
{
    return 1; /* dummy, non-MACH_PORT_NULL port name */
}

int
mk_timer_destroy(unsigned int name)
{
    (void)name;
    return 0;
}

int
mk_timer_arm(unsigned int name, uint64_t expire_time)
{
    (void)name; (void)expire_time;
    return 0;
}

int
mk_timer_cancel(unsigned int name, uint64_t *result_time)
{
    (void)name;
    if (result_time) *result_time = 0;
    return 0;
}

int
task_threads(unsigned int task, unsigned int **thread_list, unsigned int *thread_count)
{
    (void)task;
    if (thread_list) *thread_list = NULL;
    if (thread_count) *thread_count = 0;
    return 5; /* KERN_FAILURE */
}

int
thread_resume(unsigned int thread)
{
    (void)thread;
    return 5; /* KERN_FAILURE */
}

int
thread_suspend(unsigned int thread)
{
    (void)thread;
    return 5; /* KERN_FAILURE */
}

qos_class_t
qos_class_self(void)
{
    return QOS_CLASS_UNSPECIFIED;
}

/*
 * modf/scalbn: real libm functions, not yet in the from-scratch libm here.
 * Minimal, correct implementations (not fast, but not stubs either).
 */
double
modf(double value, double *iptr)
{
    /* Truncate toward zero via a double -> int64_t -> double round trip
     * (avoids __builtin_floor(), which degrades to a real libm floor()
     * call we don't have under -nostdlib) - correct for any value that
     * fits in an int64_t, which covers every practical caller. */
    double ival = (double)(int64_t)value;
    if (iptr) *iptr = ival;
    return value - ival;
}

double
scalbn(double x, int n)
{
    /* x * 2^n via direct manipulation of the IEEE 754 exponent field -
     * __builtin_exp2() degrades to a real libm exp2() call we don't have
     * under -nostdlib, so avoid depending on any float library routine. */
    while (n > 1023) { x *= 0x1p1023; n -= 1023; }
    while (n < -1022) { x *= 0x1p-1022; n += 1022; }
    union { double d; uint64_t u; } scale;
    scale.u = (uint64_t)(n + 1023) << 52;
    return x * scale.d;
}

/*
 * os_log_create: CF calls this directly (not through the os_log_create ->
 * _os_log_create macro redirect real Apple's os/log.h defines - CF must be
 * built against a copy of that header without the redirect, or without
 * OS_LOG_TARGET_HAS_10_12_FEATURES). Same stub behavior as _os_log_create
 * above, under the plain (non-underscore) name CF's own object files
 * actually reference.
 */
void *
os_log_create(const char *subsystem, const char *category)
{
    (void)subsystem; (void)category;
    static int dummy_log_handle;
    return &dummy_log_handle;
}

/*
 * __udivti3: compiler-rt's 128-bit unsigned division, needed whenever code
 * divides an unsigned __int128 (CFBigNumber's 128-bit arithmetic here) on a
 * target without native 128-bit division. No prebuilt libclang_rt.builtins
 * for this cross target, so implement the well-known compiler-rt algorithm
 * directly: binary long division, one bit at a time. Not fast, but CF only
 * needs this for occasional big-number formatting, not a hot path.
 */
unsigned __int128
__udivti3(unsigned __int128 a, unsigned __int128 b)
{
    if (b == 0) return 0; /* real compiler-rt traps; we don't have one to trap into */
    unsigned __int128 quotient = 0;
    unsigned __int128 remainder = 0;
    for (int i = 127; i >= 0; i--) {
        remainder = (remainder << 1) | ((a >> i) & 1);
        if (remainder >= b) {
            remainder -= b;
            quotient |= ((unsigned __int128)1 << i);
        }
    }
    return quotient;
}


/*
 * slot_name (mach/mach_init.h): hostinfo(1)'s only non-MIG dependency.
 * Real Darwin has a large historical table covering every CPU type/subtype
 * combination it ever ran on (68k, PowerPC, ARM, ...); PureDarwin only ever
 * targets CPU_TYPE_X86_64, so give real names for that family and a generic
 * fallback for anything else rather than porting the whole table.
 */
#include <mach/machine.h>

void
slot_name(cpu_type_t cpu_type, cpu_subtype_t cpu_subtype, char **cpu_name, char **cpu_subname)
{
    static char subname_buf[32];

    if (cpu_type == CPU_TYPE_X86_64) {
        *cpu_name = "x86_64";
        switch (cpu_subtype & ~CPU_SUBTYPE_MASK) {
        case CPU_SUBTYPE_X86_64_ALL:
            *cpu_subname = "all";
            return;
        case CPU_SUBTYPE_X86_64_H:
            *cpu_subname = "Haswell";
            return;
        default:
            break;
        }
    } else if (cpu_type == CPU_TYPE_I386) {
        *cpu_name = "i386";
        *cpu_subname = "all";
        return;
    } else {
        *cpu_name = "unknown";
    }

    snprintf(subname_buf, sizeof(subname_buf), "subtype %d", cpu_subtype);
    *cpu_subname = subname_buf;
}

/*
 * Batch from the full-image import sweep (2026-07-18): every symbol below is
 * a lazy bind somewhere in the shipped image (i3/glib family, ICU, CF,
 * fastfetch) that would abort the process the first time it's called.
 */

/* creat(2): historical alias for open(O_WRONLY|O_CREAT|O_TRUNC). */
int
creat(const char *path, mode_t mode)
{
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

/*
 * close$NOCANCEL: the non-cancelable variant glib picks up from the SDK
 * headers. PD's close is not a pthread cancellation point anyway, so the
 * plain syscall wrapper is exactly the right behavior.
 */
int __pd_close_nocancel(int fd) __asm("_close$NOCANCEL");
int
__pd_close_nocancel(int fd)
{
    return close(fd);
}

/*
 * realpath: stdlib.h decorates the definition in stdlib/FreeBSD/realpath.c
 * as _realpath$DARWIN_EXTSN, but ICU (built against a plain-POSIX profile)
 * imports the undecorated _realpath. Forward one to the other.
 */
extern char *__pd_realpath_extsn(const char * __restrict, char * __restrict)
    __asm("_realpath$DARWIN_EXTSN");
char *__pd_realpath_plain(const char * __restrict, char * __restrict)
    __asm("_realpath");
char *
__pd_realpath_plain(const char * __restrict path, char * __restrict resolved)
{
    return __pd_realpath_extsn(path, resolved);
}

/*
 * dlopen_preflight(3): forwarded to dyld the same way as dlopen/dlsym above.
 * CF uses it to probe bundles before loading them.
 */
bool
dlopen_preflight(const char *path)
{
    typedef bool (*preflight_fn)(const char *, void *);
    static preflight_fn fn;

    if (fn == NULL &&
        !_dyld_func_lookup("__dyld_dlopen_preflight_internal", (void **)&fn)) {
        return false;
    }

    return fn(path, __builtin_return_address(0));
}

/*
 * fmtcheck(3): validate that `fmt` has the same conversion specifiers as
 * `fmt_default`; return fmt if compatible, fmt_default otherwise. Used by
 * file(1) for magic-supplied format strings. This is a conservative
 * implementation: it compares the ordered list of conversion characters
 * (skipping flags/width/precision, honoring %% and length modifiers only to
 * find the conversion char) and rejects on any mismatch.
 */
static const char *
__pd_fmt_next_conv(const char *p, char *out)
{
    while ((p = strchr(p, '%')) != NULL) {
        p++;
        if (*p == '%') { p++; continue; }
        while (*p != '\0' && strchr("#0- +'123456789.*hljtzq", *p) != NULL)
            p++;
        if (*p == '\0')
            return NULL;
        *out = *p;
        return p + 1;
    }
    return NULL;
}

const char *
fmtcheck(const char *fmt, const char *fmt_default)
{
    const char *a = fmt, *b = fmt_default;
    char ca, cb;

    if (fmt == NULL)
        return fmt_default;

    for (;;) {
        a = __pd_fmt_next_conv(a, &ca);
        b = __pd_fmt_next_conv(b, &cb);
        if (a == NULL && b == NULL)
            return fmt;
        if (a == NULL || b == NULL || ca != cb)
            return fmt_default;
    }
}

/*
 * swap_fat_header/swap_fat_arch/swap_fat_arch_64 (libmacho): fat headers are
 * stored big-endian; these unconditionally byte-swap every field (the real
 * ones take a target byte order argument, but the only sensible use on a
 * little-endian host is a full swap, which is also an involution).
 * fastfetch uses them to inspect fat binaries.
 */
struct __pd_fat_header { uint32_t magic, nfat_arch; };
struct __pd_fat_arch { uint32_t cputype, cpusubtype, offset, size, align; };
struct __pd_fat_arch_64 {
    uint32_t cputype, cpusubtype;
    uint64_t offset, size;
    uint32_t align, reserved;
};

void
swap_fat_header(struct __pd_fat_header *h, int target_byte_order)
{
    (void)target_byte_order;
    h->magic = __builtin_bswap32(h->magic);
    h->nfat_arch = __builtin_bswap32(h->nfat_arch);
}

void
swap_fat_arch(struct __pd_fat_arch *a, uint32_t n, int target_byte_order)
{
    (void)target_byte_order;
    for (uint32_t i = 0; i < n; i++) {
        a[i].cputype = __builtin_bswap32(a[i].cputype);
        a[i].cpusubtype = __builtin_bswap32(a[i].cpusubtype);
        a[i].offset = __builtin_bswap32(a[i].offset);
        a[i].size = __builtin_bswap32(a[i].size);
        a[i].align = __builtin_bswap32(a[i].align);
    }
}

void
swap_fat_arch_64(struct __pd_fat_arch_64 *a, uint32_t n, int target_byte_order)
{
    (void)target_byte_order;
    for (uint32_t i = 0; i < n; i++) {
        a[i].cputype = __builtin_bswap32(a[i].cputype);
        a[i].cpusubtype = __builtin_bswap32(a[i].cpusubtype);
        a[i].offset = __builtin_bswap64(a[i].offset);
        a[i].size = __builtin_bswap64(a[i].size);
        a[i].align = __builtin_bswap32(a[i].align);
        a[i].reserved = __builtin_bswap32(a[i].reserved);
    }
}

/*
 * strxfrm: the FreeBSD source needs the __collate_* locale machinery, which
 * PD doesn't build (C locale only). In the C locale strxfrm is defined to be
 * a plain copy whose result compares like strcmp - i.e. strlcpy semantics.
 */
size_t
strxfrm(char * __restrict dst, const char * __restrict src, size_t n)
{
    size_t len = strlen(src);

    if (n != 0) {
        size_t copy = (len >= n) ? n - 1 : len;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return len;
}


/*
 * qsort_b: referenced by gen/fts.c for the fts_open_b (block comparator)
 * path. Implemented on top of the real FreeBSD qsort_r with a trampoline
 * that invokes the block. Blocks-ABI note: a block pointer is a struct whose
 * 4th word is the invoke function taking the block itself as the hidden
 * first argument; calling it directly avoids needing the BlocksRuntime.
 */
struct __pd_block_layout {
    void *isa;
    int flags;
    int reserved;
    int (*invoke)(void *block, const void *a, const void *b);
};

extern void qsort_r(void *, size_t, size_t, void *,
    int (*)(void *, const void *, const void *));

static int
__pd_qsort_b_thunk(void *block, const void *a, const void *b)
{
    struct __pd_block_layout *bl = block;
    return bl->invoke(bl, a, b);
}

void __pd_qsort_b(void *base, size_t nel, size_t width, void *block)
    __asm("_qsort_b");
void
__pd_qsort_b(void *base, size_t nel, size_t width, void *block)
{
    qsort_r(base, nel, width, block, __pd_qsort_b_thunk);
}
