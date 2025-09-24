#ifndef FILE_MONITOR_H
#define FILE_MONITOR_H

typedef struct file_monitor_t file_monitor;


typedef void (*file_change_callback)(const char* path, int event_type);

file_monitor* file_monitor_new(file_change_callback callback);
void file_monitor_destroy(file_monitor* monitor);

int file_monitor_add_path(file_monitor* monitor, const char* path);
int file_monitor_add_paths(file_monitor* monitor, const char** paths, int path_count);
int file_monitor_remove_paths(file_monitor* monitor, const char** paths, int path_count);
int file_monitor_remove_path(file_monitor* monitor, const char* path);
int file_monitor_add_paths_recursive(file_monitor* monitor, const char** paths, int path_count);

#endif /* FILE_MONITOR_H */
