
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>

#include <sys/epoll.h>

#include <linux/input.h>
// #include <linux/eventpoll.h>

#define MAX_NAME_LEN 256

#define MAX_DEVICES 16
#define MAX_MISC_FDS 16

#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define BITS_TO_LONGS(x) (((x) + BITS_PER_LONG - 1) / BITS_PER_LONG)

#define test_bit(bit, array) \
    ((array)[(bit)/BITS_PER_LONG] & (1 << ((bit) % BITS_PER_LONG)))

typedef int(*ev_callback)(int, uint32_t, void*);

struct fd_info {
    int fd;
    char dev[MAX_NAME_LEN];
    ev_callback cb;
    void *data;
};

static int capture_auto = 0;
static int list_only = 0;
static char capture_name[MAX_NAME_LEN] = {0};

static int epollfd;
static struct epoll_event polledevents[MAX_DEVICES + MAX_MISC_FDS];
static int npolledevents;

static struct fd_info ev_fdinfo[MAX_DEVICES + MAX_MISC_FDS];

static unsigned ev_count = 0;
static unsigned ev_dev_count = 0;
static unsigned ev_misc_count = 0;

static int input_callback(int fd, uint32_t epevents, void* data);
static int ev_get_input(int fd, uint32_t epevents, struct input_event *ev);
static void show_event(const char* dev, struct input_event* ev);
static const char* get_type_name(__u16 type);
static int ev_init(ev_callback input_cb);
static void ev_exit(void);
static int ev_wait(int timeout);
static void ev_dispatch(void);

static void signal_func(int sig_num)
{
    printf("catch signal %d\n", sig_num);
    
    ev_exit();
    exit(0);
}

static void usage(const char* exec)
{
    printf("Usage: %s [-v/l/h] [-d device]\n"
           "  -a Capture the events automallically\n"
           "  -d Set a capture device\n"
           "  -l List all available device\n"
           "  -h See the usage\n"
           "eg.:\n"
           "  %s -a\n"
           "  %s -l\n"
           "  %s -d adc-keys\n",
           exec, exec, exec, exec);
}

int main(int argc, char **argv)
{
    int opt;
    char *prog = argv[0];

    while ((opt = getopt(argc, argv, "ald:h")) != -1) {
        switch (opt) {
        case 'a':
            capture_auto = 1;
            break;
        case 'l':
            list_only = 1;
            break;
        case 'd':
            strncpy(capture_name, optarg, sizeof(capture_name));
            break;
        case 'h':
        default:
            usage(argv[0]);
            return 0;
        }
    }

    if (argc == 1) {
        usage(argv[0]);
        return 0;
    }

    if (ev_init(input_callback) < 0)
        return -1;

    signal(SIGINT, signal_func);
    
    if (!list_only) {
        for (;;) {
            if (!ev_wait(-1))
                ev_dispatch();
        }
    }

    ev_exit();

    return 0;
}

static int input_callback(int fd, uint32_t epevents, void* data)
{
    struct fd_info* info = (struct fd_info *)data;
    char* dev = info ? info->dev : "";
    struct input_event ev;
    int err = ev_get_input(fd, epevents, &ev);

    if (err) return -1;

    show_event(dev, &ev);

    return 0;
}

static void show_event(const char* dev, struct input_event* ev)
{
    int msecs = (ev->time.tv_usec / 1000) % 1000;
    int secs = (msecs / 1000) % 1000;

    printf("%s : %s %4d %4d +%d.%dsec\n",
        dev, get_type_name(ev->type), ev->code, ev->value, secs, msecs);
    if (ev->type == EV_SYN)
        printf("\n");
}

static const char* get_type_name(__u16 type)
{
    switch (type) {
        case EV_SYN: return "EV_SYN";
        case EV_KEY: return "EV_KEY";
        case EV_REL: return "EV_REL";
        case EV_ABS: return "EV_ABS";
        case EV_MSC: return "EV_MSC";
        case EV_SW:  return "EV_SW";
        case EV_LED: return "EV_LED";
        case EV_SND: return "EV_SND";
        case EV_FF:  return "EV_FF";
        case EV_PWR: return "EV_PWR";
        case EV_FF_STATUS: return "EV_FF_STATUS";
    }
    
    return "???";
}

static int ev_init(ev_callback input_cb)
{
    int fd;
    DIR *dir;
    struct dirent *de;
    char name[256];
    struct epoll_event ev;
    int epollctlfail = 0;
    int count = 0;
    int match = 0;

    epollfd = epoll_create(MAX_DEVICES + MAX_MISC_FDS);
    
    if (epollfd == -1) return -1;

    if (list_only) {
        printf("List devices:\n---\n");
    }

    dir = opendir("/dev/input");
    if(dir != 0) {
        while((de = readdir(dir))) {
            unsigned long ev_bits[BITS_TO_LONGS(EV_MAX)];

//            fprintf(stderr,"/dev/input/%s\n", de->d_name);
            if(strncmp(de->d_name,"event",5)) continue;
            fd = openat(dirfd(dir), de->d_name, O_RDONLY);
            if(fd < 0) continue;

            /* read the evbits of the input device */
            if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) {
                close(fd);
                continue;
            }

            /* TODO: add ability to specify event masks. For now, just assume
             * that only EV_KEY and EV_REL event types are ever needed. */
            if (!test_bit(EV_KEY, ev_bits) && !test_bit(EV_REL, ev_bits)) {
                close(fd);
                continue;
            }

            // ev.events = EPOLLIN | EPOLLWAKEUP;
            ev.events = EPOLLIN;
            ev.data.ptr = (void *)&ev_fdinfo[ev_count];
            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev)) {
                close(fd);
                epollctlfail = 1;
                continue;
            }

            if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
                if (capture_auto || list_only) {
                    printf("%d. %s\n", count, name);
                }
                count++;
                strncpy(ev_fdinfo[ev_count].dev, name, strlen(name));
            }

            ev_fdinfo[ev_count].fd = fd;
            ev_fdinfo[ev_count].cb = input_cb;
            ev_fdinfo[ev_count].data = &ev_fdinfo[ev_count];
            ev_count++;
            ev_dev_count++;
            if(ev_dev_count == MAX_DEVICES) break;

            if (capture_name[0] && strncmp(capture_name, name, MAX_NAME_LEN) == 0) {
                printf("%s has match.\n", name);
                match = 1;
                break;
            }
        }
    }

    if (epollctlfail && !ev_count) {
        close(epollfd);
        epollfd = -1;
        return -1;
    }

    if (capture_name[0] && match == 0) {
        close(epollfd);
        epollfd = -1;
        printf("%s not match.\n", name);
        return -2;
    }

    return 0;
}

static void ev_exit(void)
{
    while (ev_count > 0)
        close(ev_fdinfo[--ev_count].fd);

    ev_misc_count = 0;
    ev_dev_count = 0;
    close(epollfd);
}

static int ev_wait(int timeout)
{
    npolledevents = epoll_wait(epollfd, polledevents, ev_count, timeout);
    
    if (npolledevents <= 0) return -1;
    
    return 0;
}

static void ev_dispatch(void)
{
    int n;

    for (n = 0; n < npolledevents; n++) {
        struct fd_info *info = polledevents[n].data.ptr;
        ev_callback callback = info->cb;
        
        if (callback)
            callback(info->fd, polledevents[n].events, info->data);
    }
}

static int ev_get_input(int fd, uint32_t epevents, struct input_event *ev)
{
    if (epevents & EPOLLIN) {
        int r = read(fd, ev, sizeof(*ev));
        
        if (r == sizeof(*ev)) return 0;
    }
    
    return -1;
}
