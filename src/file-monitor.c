#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <libgen.h>
#include "file-monitor.h"

#define EVENT_BUF_LEN     (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))
#define WATCH_FLAGS       (IN_CLOSE_WRITE | IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO)

typedef void (*file_change_callback)(const char* path, int event_type);

typedef struct {
    char* filename;
    char* full_path;
} monitored_file_t;

typedef struct {
    char* dir_path;
    int watch_descriptor;
    monitored_file_t** files;
    int file_count;
    int file_capacity;
} directory_watch_t;

struct file_monitor_t {
    int inotify_fd;
    int shutdown_pipe[2];
    pthread_t watch_thread;
    pthread_mutex_t lock;
    directory_watch_t** directories;
    int dir_count;
    int dir_capacity;
    file_change_callback callback;
};

static void* watch_thread_func(void* arg);
static directory_watch_t* find_directory_watch(file_monitor* monitor, const char* dir_path);
static directory_watch_t* add_directory_watch(file_monitor* monitor, const char* dir_path);
static monitored_file_t* find_monitored_file(directory_watch_t* dir_watch, const char* filename);
static void add_monitored_file(directory_watch_t* dir_watch, const char* filename, const char* full_path);
static void remove_monitored_file(directory_watch_t* dir_watch, const char* filename);
static void cleanup_directory_watch(file_monitor* monitor, directory_watch_t* dir_watch);

file_monitor* file_monitor_new(file_change_callback callback) {
    if (!callback) {
        return NULL;
    }

    file_monitor* monitor = (file_monitor*)malloc(sizeof(file_monitor));
    if (!monitor) {
        return NULL;
    }

    monitor->inotify_fd = inotify_init();
    if (monitor->inotify_fd == -1) {
        free(monitor);
        return NULL;
    }

    if (pipe(monitor->shutdown_pipe) == -1) {
        close(monitor->inotify_fd);
        free(monitor);
        return NULL;
    }

    if (pthread_mutex_init(&monitor->lock, NULL) != 0) {
        close(monitor->shutdown_pipe[0]);
        close(monitor->shutdown_pipe[1]);
        close(monitor->inotify_fd);
        free(monitor);
        return NULL;
    }

    monitor->dir_capacity = 10;
    monitor->dir_count = 0;
    monitor->directories = (directory_watch_t**)malloc(
        monitor->dir_capacity * sizeof(directory_watch_t*));

    if (!monitor->directories) {
        pthread_mutex_destroy(&monitor->lock);
        close(monitor->shutdown_pipe[0]);
        close(monitor->shutdown_pipe[1]);
        close(monitor->inotify_fd);
        free(monitor);
        return NULL;
    }

    monitor->callback = callback;

    if (pthread_create(&monitor->watch_thread, NULL, watch_thread_func, monitor) != 0) {
        free(monitor->directories);
        pthread_mutex_destroy(&monitor->lock);
        close(monitor->shutdown_pipe[0]);
        close(monitor->shutdown_pipe[1]);
        close(monitor->inotify_fd);
        free(monitor);
        return NULL;
    }

    return monitor;
}

void file_monitor_destroy(file_monitor* monitor) {
    if (!monitor) {
        return;
    }

    char shutdown_signal = 1;
    write(monitor->shutdown_pipe[1], &shutdown_signal, 1);

    pthread_join(monitor->watch_thread, NULL);

    for (int i = 0; i < monitor->dir_count; i++) {
        cleanup_directory_watch(monitor, monitor->directories[i]);
    }
    free(monitor->directories);

    pthread_mutex_destroy(&monitor->lock);
    close(monitor->shutdown_pipe[0]);
    close(monitor->shutdown_pipe[1]);
    close(monitor->inotify_fd);
    free(monitor);
}

static void* watch_thread_func(void* arg) {
    file_monitor* monitor = (file_monitor*)arg;
    char buffer[EVENT_BUF_LEN];
    fd_set read_fds;
    int max_fd = monitor->inotify_fd > monitor->shutdown_pipe[0] ?
                 monitor->inotify_fd : monitor->shutdown_pipe[0];

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(monitor->inotify_fd, &read_fds);
        FD_SET(monitor->shutdown_pipe[0], &read_fds);

        int result = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if (result == -1) {
            break;
        }

        if (FD_ISSET(monitor->shutdown_pipe[0], &read_fds)) {
            break;
        }

        if (FD_ISSET(monitor->inotify_fd, &read_fds)) {
            ssize_t len = read(monitor->inotify_fd, buffer, EVENT_BUF_LEN);

            if (len <= 0) {
                continue;
            }

            pthread_mutex_lock(&monitor->lock);

            ssize_t i = 0;
            while (i < len) {
                struct inotify_event* event = (struct inotify_event*)&buffer[i];

                directory_watch_t* dir_watch = NULL;
                for (int j = 0; j < monitor->dir_count; j++) {
                    if (monitor->directories[j]->watch_descriptor == event->wd) {
                        dir_watch = monitor->directories[j];
                        break;
                    }
                }

                if (dir_watch && event->len > 0) {
                    char full_path[PATH_MAX];
                    snprintf(full_path, sizeof(full_path), "%s/%s", dir_watch->dir_path, event->name);
                    monitor->callback(full_path, event->mask);
                }

                i += sizeof(struct inotify_event) + event->len;
            }

            pthread_mutex_unlock(&monitor->lock);
        }
    }

    return NULL;
}

static directory_watch_t* find_directory_watch(file_monitor* monitor, const char* dir_path) {
    for (int i = 0; i < monitor->dir_count; i++) {
        if (strcmp(monitor->directories[i]->dir_path, dir_path) == 0) {
            return monitor->directories[i];
        }
    }
    return NULL;
}

static directory_watch_t* add_directory_watch(file_monitor* monitor, const char* dir_path) {
    if (monitor->dir_count >= monitor->dir_capacity) {
        int new_capacity = monitor->dir_capacity * 2;
        directory_watch_t** new_dirs = (directory_watch_t**)realloc(
            monitor->directories, new_capacity * sizeof(directory_watch_t*));

        if (!new_dirs) {
            return NULL;
        }

        monitor->directories = new_dirs;
        monitor->dir_capacity = new_capacity;
    }

    int wd = inotify_add_watch(monitor->inotify_fd, dir_path, WATCH_FLAGS);
    if (wd == -1) {
        return NULL;
    }

    directory_watch_t* dir_watch = (directory_watch_t*)malloc(sizeof(directory_watch_t));
    if (!dir_watch) {
        inotify_rm_watch(monitor->inotify_fd, wd);
        return NULL;
    }

    dir_watch->dir_path = strdup(dir_path);
    dir_watch->watch_descriptor = wd;
    dir_watch->file_capacity = 10;
    dir_watch->file_count = 0;
    dir_watch->files = (monitored_file_t**)malloc(
        dir_watch->file_capacity * sizeof(monitored_file_t*));

    if (!dir_watch->dir_path || !dir_watch->files) {
        if (dir_watch->dir_path) free(dir_watch->dir_path);
        if (dir_watch->files) free(dir_watch->files);
        free(dir_watch);
        inotify_rm_watch(monitor->inotify_fd, wd);
        return NULL;
    }

    monitor->directories[monitor->dir_count++] = dir_watch;

    return dir_watch;
}

static monitored_file_t* find_monitored_file(directory_watch_t* dir_watch, const char* filename) {
    for (int i = 0; i < dir_watch->file_count; i++) {
        if (strcmp(dir_watch->files[i]->filename, filename) == 0) {
            return dir_watch->files[i];
        }
    }
    return NULL;
}

static void add_monitored_file(directory_watch_t* dir_watch, const char* filename, const char* full_path) {
    if (dir_watch->file_count >= dir_watch->file_capacity) {
        int new_capacity = dir_watch->file_capacity * 2;
        monitored_file_t** new_files = (monitored_file_t**)realloc(
            dir_watch->files, new_capacity * sizeof(monitored_file_t*));

        if (!new_files) {
            return;
        }

        dir_watch->files = new_files;
        dir_watch->file_capacity = new_capacity;
    }

    monitored_file_t* file = (monitored_file_t*)malloc(sizeof(monitored_file_t));
    if (!file) {
        return;
    }

    file->filename = strdup(filename);
    file->full_path = strdup(full_path);

    if (!file->filename || !file->full_path) {
        if (file->filename) free(file->filename);
        if (file->full_path) free(file->full_path);
        free(file);
        return;
    }

    dir_watch->files[dir_watch->file_count++] = file;
}

static void remove_monitored_file(directory_watch_t* dir_watch, const char* filename) {
    for (int i = 0; i < dir_watch->file_count; i++) {
        if (strcmp(dir_watch->files[i]->filename, filename) == 0) {
            free(dir_watch->files[i]->filename);
            free(dir_watch->files[i]->full_path);
            free(dir_watch->files[i]);

            for (int j = i; j < dir_watch->file_count - 1; j++) {
                dir_watch->files[j] = dir_watch->files[j + 1];
            }

            dir_watch->file_count--;
            return;
        }
    }
}

static void cleanup_directory_watch(file_monitor* monitor, directory_watch_t* dir_watch) {
    inotify_rm_watch(monitor->inotify_fd, dir_watch->watch_descriptor);

    for (int i = 0; i < dir_watch->file_count; i++) {
        free(dir_watch->files[i]->filename);
        free(dir_watch->files[i]->full_path);
        free(dir_watch->files[i]);
    }

    free(dir_watch->files);
    free(dir_watch->dir_path);
    free(dir_watch);
}

int file_monitor_add_paths(file_monitor* monitor, const char** paths, int path_count) {
    if (!monitor || !paths || path_count <= 0) {
        return -1;
    }

    pthread_mutex_lock(&monitor->lock);

    for (int i = 0; i < path_count; i++) {
        const char* dir_path = paths[i];

        directory_watch_t* existing = find_directory_watch(monitor, dir_path);
        if (existing) {
            continue;
        }

        directory_watch_t* dir_watch = add_directory_watch(monitor, dir_path);
        if (!dir_watch) {
            continue;
        }

    }

    pthread_mutex_unlock(&monitor->lock);
    return 0;
}

int file_monitor_remove_paths(file_monitor* monitor, const char** paths, int path_count) {
    if (!monitor || !paths || path_count <= 0) {
        return -1;
    }

    pthread_mutex_lock(&monitor->lock);

    for (int i = 0; i < path_count; i++) {
        const char* dir_path = paths[i];

        int dir_index = -1;
        for (int j = 0; j < monitor->dir_count; j++) {
            if (strcmp(monitor->directories[j]->dir_path, dir_path) == 0) {
                dir_index = j;
                break;
            }
        }

        if (dir_index >= 0) {
            cleanup_directory_watch(monitor, monitor->directories[dir_index]);

            for (int j = dir_index; j < monitor->dir_count - 1; j++) {
                monitor->directories[j] = monitor->directories[j + 1];
            }

            monitor->dir_count--;
        } else {
        }
    }

    pthread_mutex_unlock(&monitor->lock);
    return 0;
}
