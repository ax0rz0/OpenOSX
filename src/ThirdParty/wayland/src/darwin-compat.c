/* Copyright (c) 2026 PureDarwin contributors. SPDX-License-Identifier: MIT */
#include "darwin-compat.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/event.h>
#include <unistd.h>

struct pd_timer {
	int fd, kq;
	void *udata;
	struct pd_timer *next;
};
struct pd_signal { int read_fd, write_fd, signal_number; struct pd_signal *next; };
static struct pd_timer *timers;
static struct pd_signal *signals[NSIG];

static void
set_nonblock_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(fd, F_GETFD);
    fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

int epoll_create(int size) { (void)size; return kqueue(); }
int
epoll_create1(int flags)
{
    int fd = kqueue();
    if (fd >= 0 && (flags & EPOLL_CLOEXEC)) {
        int old = fcntl(fd, F_GETFD);
        fcntl(fd, F_SETFD, old | FD_CLOEXEC);
    }
    return fd;
}

static int
change_filter(int kq, int fd, int filter, uint16_t flags, void *udata)
{
    struct kevent change;
    EV_SET(&change, (uintptr_t)fd, filter, flags, 0, 0, udata);
    return kevent(kq, &change, 1, NULL, 0, NULL);
}

int
epoll_ctl(int epfd, int operation, int fd, struct epoll_event *event)
{
    struct pd_timer *timer;
    /* No EV_CLEAR: callers use epoll's default level-triggered semantics and
     * handle one item per wakeup rather than draining the fd. Edge-triggered
     * loses everything that arrived before the handler ran - a second client
     * connecting while the first is still queued is never accepted. */
    uint16_t flags = EV_ADD | EV_ENABLE;
    if (operation == EPOLL_CTL_DEL || operation == EPOLL_CTL_MOD) {
        change_filter(epfd, fd, EVFILT_READ, EV_DELETE, NULL);
        change_filter(epfd, fd, EVFILT_WRITE, EV_DELETE, NULL);
        change_filter(epfd, fd, EVFILT_TIMER, EV_DELETE, NULL);
        if (operation == EPOLL_CTL_DEL) return 0;
    }
    for (timer = timers; timer; timer = timer->next) {
        if (timer->fd == fd) {
            timer->kq = epfd;
            timer->udata = event->data.ptr;
            return 0;
        }
    }
    if ((event->events & EPOLLIN) &&
        change_filter(epfd, fd, EVFILT_READ, flags, event->data.ptr) < 0)
        return -1;
    if ((event->events & EPOLLOUT) &&
        change_filter(epfd, fd, EVFILT_WRITE, flags, event->data.ptr) < 0)
        return -1;
    return 0;
}

int
epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    struct kevent ready[64];
    struct timespec ts;
    int count, i;
    if (maxevents <= 0 || maxevents > 64) { errno = EINVAL; return -1; }
    if (timeout < 0) count = kevent(epfd, NULL, 0, ready, maxevents, NULL);
    else {
        ts.tv_sec = timeout / 1000;
        ts.tv_nsec = (timeout % 1000) * 1000000L;
        count = kevent(epfd, NULL, 0, ready, maxevents, &ts);
    }
    if (count < 0) return -1;
    for (i = 0; i < count; i++) {
        events[i].events = 0;
        if (ready[i].filter == EVFILT_READ) events[i].events |= EPOLLIN;
        if (ready[i].filter == EVFILT_WRITE) events[i].events |= EPOLLOUT;
        if (ready[i].flags & EV_EOF) events[i].events |= EPOLLHUP;
        if (ready[i].flags & EV_ERROR) events[i].events |= EPOLLERR;
        events[i].data.ptr = ready[i].udata;
    }
    return count;
}

int
timerfd_create(int clockid, int flags)
{
    struct pd_timer *timer;
    int fd;
    (void)clockid;
    fd = open("/dev/null", O_RDONLY);
    if (fd < 0) return -1;
    if (flags & TFD_CLOEXEC) set_nonblock_cloexec(fd);
    timer = calloc(1, sizeof(*timer));
    if (!timer) { close(fd); errno = ENOMEM; return -1; }
    timer->fd = fd; timer->kq = -1; timer->next = timers; timers = timer;
    return fd;
}

int
timerfd_settime(int fd, int flags, const struct itimerspec *value,
                struct itimerspec *old_value)
{
    struct pd_timer *timer;
    struct kevent change;
    struct timespec now;
    int64_t milliseconds;
    (void)flags; (void)old_value;
    for (timer = timers; timer && timer->fd != fd; timer = timer->next);
    if (!timer || timer->kq < 0) { errno = EINVAL; return -1; }
    EV_SET(&change, (uintptr_t)fd, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    kevent(timer->kq, &change, 1, NULL, 0, NULL);
    if (!value->it_value.tv_sec && !value->it_value.tv_nsec) return 0;
    clock_gettime(CLOCK_MONOTONIC, &now);
    milliseconds = (int64_t)(value->it_value.tv_sec - now.tv_sec) * 1000;
    milliseconds += (value->it_value.tv_nsec - now.tv_nsec) / 1000000;
    if (milliseconds < 1) milliseconds = 1;
    EV_SET(&change, (uintptr_t)fd, EVFILT_TIMER,
           EV_ADD | EV_ENABLE | EV_ONESHOT, NOTE_MSECONDS, milliseconds,
           timer->udata);
    return kevent(timer->kq, &change, 1, NULL, 0, NULL);
}

static void
pd_signal_handler(int signal_number)
{
    struct signalfd_siginfo info = { (uint32_t)signal_number };
    struct pd_signal *signal = signals[signal_number];
    if (signal) (void)write(signal->write_fd, &info, sizeof(info));
}

int
signalfd(int fd, const sigset_t *mask, int flags)
{
    struct pd_signal *signal;
    struct sigaction action;
    int signal_number, pipe_fds[2];
    (void)fd; (void)flags;
    if (!mask) { errno = EINVAL; return -1; }
    for (signal_number = 1; signal_number < NSIG; signal_number++)
        if (sigismember(mask, signal_number) == 1) break;
    if (signal_number == NSIG || pipe(pipe_fds) < 0) return -1;
    set_nonblock_cloexec(pipe_fds[0]); set_nonblock_cloexec(pipe_fds[1]);
    signal = calloc(1, sizeof(*signal));
    if (!signal) { close(pipe_fds[0]); close(pipe_fds[1]); errno = ENOMEM; return -1; }
    signal->read_fd = pipe_fds[0]; signal->write_fd = pipe_fds[1];
    signal->signal_number = signal_number; signal->next = signals[signal_number];
    signals[signal_number] = signal;
    action.sa_handler = pd_signal_handler; sigemptyset(&action.sa_mask); action.sa_flags = 0;
    sigaction(signal_number, &action, NULL);
    return pipe_fds[0];
}
