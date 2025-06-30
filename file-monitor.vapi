[CCode (cheader_filename = "file-monitor.h")]
namespace INotify {
    [CCode (has_type_id=false, has_target = false)]
    public delegate void ChangeCallback (string path, int event_type);

    [CCode (cname = "file_monitor", free_function = "file_monitor_destroy")]
    [Compact]
    public class Monitor {
        [CCode (cname = "file_monitor_new")]
        public Monitor (ChangeCallback callback);

        [CCode (cname = "file_monitor_add_paths")]
        public int add_paths ([CCode (array_length_pos = 1.1)] string[] paths);

        [CCode (cname = "file_monitor_add_paths_recursive")]
        public int add_paths_recursive ([CCode (array_length_pos = 1.1)] string[] paths);

        [CCode (cname = "file_monitor_remove_paths")]
        public int remove_paths ([CCode (array_length_pos = 1.1)] string[] paths);
    }

    [CCode (cheader_filename = "sys/inotify.h", cprefix = "IN_", has_type_id = false)]
    [Flags]
    public enum EventType {
        ACCESS,
        MODIFY,
        ATTRIB,
        CLOSE_WRITE,
        CLOSE_NOWRITE,
        OPEN,
        MOVED_FROM,
        MOVED_TO,
        CREATE,
        DELETE,
        DELETE_SELF,
        MOVE_SELF,
        CLOSE,
        MOVE,
        ONLYDIR,
        DONT_FOLLOW,
        EXCL_UNLINK,
        MASK_ADD,
        ISDIR,
        ONESHOT,
        ALL_EVENTS
    }
}
