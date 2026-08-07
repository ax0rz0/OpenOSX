#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *
default_client(void)
{
    if (access("/bin/i3", X_OK) == 0) {
        return "/bin/i3";
    }
    return "/bin/xeyes";
}

static const char *
map_client(const char *client)
{
    if (client == NULL || client[0] == '\0') {
        return default_client();
    }
    if (strcmp(client, "i3") == 0) {
        return "/bin/i3";
    }
    if (strcmp(client, "xeyes") == 0) {
        return "/bin/xeyes";
    }
    if (strcmp(client, "xterm") == 0) {
        return "/bin/xterm";
    }
    if (strcmp(client, "xterm-default") == 0) {
        return "/bin/xterm";
    }
    return client;
}

static int
is_self_arg(const char *arg)
{
    return arg != NULL &&
        (strcmp(arg, "startx") == 0 || strcmp(arg, "/bin/startx") == 0);
}

static void
exec_or_die(const char *path, char *const argv[])
{
    execv(path, argv);
    fprintf(stderr, "startx: exec %s failed: %s\n", path, strerror(errno));
    _exit(127);
}

static void
dump_file(const char *path)
{
    FILE *fp;
    int ch;

    fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "startx: cannot read %s: %s\n", path, strerror(errno));
        return;
    }

    fprintf(stderr, "startx: ----- %s -----\n", path);
    while ((ch = fgetc(fp)) != EOF) {
        fputc(ch, stderr);
    }
    fprintf(stderr, "startx: ----- end %s -----\n", path);
    fclose(fp);
}

static int
wait_for(pid_t pid)
{
    int status;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            fprintf(stderr, "startx: wait failed for pid %d: %s\n", pid, strerror(errno));
            return 1;
        }
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

static int
poll_xorg_startup(pid_t xpid)
{
    int status;
    int i;

    for (i = 0; i < 10; i++) {
        pid_t waited = waitpid(xpid, &status, WNOHANG);
        if (waited == xpid) {
            fprintf(stderr, "startx: Xorg exited before the client was launched\n");
            dump_file("/var/log/Xorg.0.log");
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
            if (WIFSIGNALED(status)) {
                return 128 + WTERMSIG(status);
            }
            return 1;
        }
        if (waited < 0 && errno != EINTR) {
            fprintf(stderr, "startx: wait Xorg startup failed: %s\n", strerror(errno));
            return 1;
        }
        sleep(1);
    }

    return 0;
}

int
main(int argc, char **argv)
{
    const char *client = default_client();
    int argi = 1;
    pid_t xpid;
    pid_t cpid;
    int rc;

    while (argi < argc && is_self_arg(argv[argi])) {
        argi++;
    }
    if (argi < argc) {
        client = map_client(argv[argi]);
    }

    fprintf(stderr, "startx: starting Xorg on :0\n");
    xpid = fork();
    if (xpid < 0) {
        fprintf(stderr, "startx: fork Xorg failed: %s\n", strerror(errno));
        return 1;
    }
    if (xpid == 0) {
        char *xargv[] = {
            "/usr/bin/Xorg",
            ":0",
            "-config",
            "/etc/X11/xorg.conf",
            "-verbose",
            "6",
            "-logverbose",
            "6",
            "-listen",
            "tcp",
            "-ac",
            NULL,
        };
        exec_or_die("/usr/bin/Xorg", xargv);
    }

    rc = poll_xorg_startup(xpid);
    if (rc != 0) {
        return rc;
    }
    setenv("DISPLAY", ":0", 1);
    setenv("FONTCONFIG_FILE", "/etc/fonts/fonts.conf", 0);
    setenv("XDG_CONFIG_DIRS", "/etc", 0);
    setenv("XDG_DATA_DIRS", "/usr/share:/share", 0);

    fprintf(stderr, "startx: launching %s on :0\n", client);
    cpid = fork();
    if (cpid < 0) {
        fprintf(stderr, "startx: fork client failed: %s\n", strerror(errno));
        kill(xpid, SIGTERM);
        (void)wait_for(xpid);
        return 1;
    }
    if (cpid == 0) {
        if (strcmp(client, "/bin/xterm") == 0) {
            char *cargv[] = {
                (char *)client,
                "-xrm",
                "XTerm*backarrowKey: false",
                NULL,
            };
            exec_or_die(client, cargv);
        } else {
            char *cargv[] = {
                (char *)client,
                NULL,
            };
            exec_or_die(client, cargv);
        }
    }

    rc = wait_for(cpid);
    kill(xpid, SIGTERM);
    (void)wait_for(xpid);
    return rc;
}
