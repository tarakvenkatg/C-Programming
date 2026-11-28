/*
 * Smart Student Attendance Management & Shortage Alert Engine
 * Written in ANSI/C11 with binary struct persistence.
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_STUDENTS 2048
#define MAX_SESSIONS 4096
#define MAX_NOTIFICATIONS 4096

typedef struct {
    double min_threshold_pct;
    double warning_threshold_pct;
} PolicyConfig;

typedef struct {
    char roll[24];
    char name[64];
    char course[24];
    int semester;
    char email[80];
    char guardian_email[80];
    int attended;
    int total;
    int alert_acknowledged;
    char last_alert_date[24];
} StudentRecord;

typedef struct {
    int session_id;
    char course[24];
    char date[20];
    char topic[80];
    int present_count;
    int absent_count;
    char absent_rolls[512];
} SessionEntry;

typedef struct {
    int notification_id;
    char roll[24];
    char student_name[64];
    char course[24];
    char channel[16];
    char recipient[160];
    char subject[128];
    char message[512];
    char sent_at[24];
    char delivery_status[48];
    int read_by_student;
} NotificationRecord;

static const char *get_data_dir(void) {
    const char *env = getenv("ATTENDANCE_DATA_DIR");
    return (env && env[0] != '\0') ? env : "data";
}

static void build_path(char *out, size_t out_sz, const char *filename) {
    snprintf(out, out_sz, "%s/%s", get_data_dir(), filename);
}

static void ensure_data_dir(void) {
    struct stat st;
    const char *dir = get_data_dir();
    if (stat(dir, &st) != 0) {
#ifdef _WIN32
        _mkdir(dir);
#else
        mkdir(dir, 0755);
#endif
    }
}

static void print_json_str(const char *s) {
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", stdout); break;
            case '\\': fputs("\\\\", stdout); break;
            case '\b': fputs("\\b", stdout); break;
            case '\f': fputs("\\f", stdout); break;
            case '\n': fputs("\\n", stdout); break;
            case '\r': fputs("\\r", stdout); break;
            case '\t': fputs("\\t", stdout); break;
            default:
                if (*p < 0x20) {
                    printf("\\u%04x", *p);
                } else {
                    putchar(*p);
                }
        }
    }
    putchar('"');
}

static int str_icontains(const char *haystack, const char *needle) {
    if (!needle || needle[0] == '\0') return 1;
    size_t nlen = strlen(needle);
    size_t hlen = strlen(haystack);
    if (nlen > hlen) return 0;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j = 0;
        while (j < nlen && tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

static int str_iequals(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void safe_copy(char *dst, size_t dst_sz, const char *src) {
    if (dst_sz == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_sz, "%s", src);
}

static PolicyConfig load_config(void) {
    PolicyConfig cfg;
    cfg.min_threshold_pct = 80.0;
    cfg.warning_threshold_pct = 85.0;

    char path[512];
    build_path(path, sizeof(path), "config.bin");
    FILE *fp = fopen(path, "rb");
    if (fp) {
        PolicyConfig disk_cfg;
        if (fread(&disk_cfg, sizeof(PolicyConfig), 1, fp) == 1) {
            if (disk_cfg.min_threshold_pct >= 1.0 && disk_cfg.min_threshold_pct <= 99.0) {
                cfg = disk_cfg;
            }
        }
        fclose(fp);
    }
    return cfg;
}

static int save_config(const PolicyConfig *cfg) {
    ensure_data_dir();
    char path[512];
    build_path(path, sizeof(path), "config.bin");
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    size_t written = fwrite(cfg, sizeof(PolicyConfig), 1, fp);
    fclose(fp);
    return written == 1;
}

static int load_students(StudentRecord *arr, int max_count) {
    char path[512];
    build_path(path, sizeof(path), "students.bin");
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    int count = 0;
    while (count < max_count && fread(&arr[count], sizeof(StudentRecord), 1, fp) == 1) {
        count++;
    }
    fclose(fp);
    return count;
}

static int save_students(const StudentRecord *arr, int count) {
    ensure_data_dir();
    char path[512];
    build_path(path, sizeof(path), "students.bin");
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    if (count > 0) {
        size_t written = fwrite(arr, sizeof(StudentRecord), (size_t)count, fp);
        fclose(fp);
        return (int)written == count;
    }
    fclose(fp);
    return 1;
}

static int load_sessions(SessionEntry *arr, int max_count) {
    char path[512];
    build_path(path, sizeof(path), "sessions.bin");
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    int count = 0;
    while (count < max_count && fread(&arr[count], sizeof(SessionEntry), 1, fp) == 1) {
        count++;
    }
    fclose(fp);
    return count;
}

static int save_sessions(const SessionEntry *arr, int count) {
    ensure_data_dir();
    char path[512];
    build_path(path, sizeof(path), "sessions.bin");
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    if (count > 0) {
        size_t written = fwrite(arr, sizeof(SessionEntry), (size_t)count, fp);
        fclose(fp);
        return (int)written == count;
    }
    fclose(fp);
    return 1;
}

static int load_notifications(NotificationRecord *arr, int max_count) {
    char path[512];
    build_path(path, sizeof(path), "notifications.bin");
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    int count = 0;
    while (count < max_count && fread(&arr[count], sizeof(NotificationRecord), 1, fp) == 1) {
        count++;
    }
    fclose(fp);
    return count;
}

static int save_notifications(const NotificationRecord *arr, int count) {
    ensure_data_dir();
    char path[512];
    build_path(path, sizeof(path), "notifications.bin");
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    if (count > 0) {
        size_t written = fwrite(arr, sizeof(NotificationRecord), (size_t)count, fp);
        fclose(fp);
        return (int)written == count;
    }
    fclose(fp);
    return 1;
}

static double calc_percentage(int attended, int total) {
    if (total <= 0) return 100.0;
    return ((double)attended * 100.0) / (double)total;
}

static const char *classify_status(double pct, const PolicyConfig *cfg) {
    if (pct + 1e-6 < cfg->min_threshold_pct) return "CRITICAL";
    if (pct + 1e-6 < cfg->warning_threshold_pct) return "WARNING";
    return "SAFE";
}

static int classes_needed_for_threshold(int attended, int total, double min_pct) {
    double pct = calc_percentage(attended, total);
    if (pct + 1e-6 >= min_pct) return 0;
    if (min_pct >= 100.0) return 9999;

    int needed = (int)ceil((min_pct * (double)total - 100.0 * (double)attended) / (100.0 - min_pct) - 1e-9);
    if (needed < 0) needed = 0;
    while (calc_percentage(attended + needed, total + needed) + 1e-6 < min_pct && needed < 5000) {
        needed++;
    }
    return needed;
}

static int safe_skips_available(int attended, int total, double min_pct) {
    double pct = calc_percentage(attended, total);
    if (pct + 1e-6 < min_pct || min_pct <= 0.0) return 0;

    int skips = (int)floor((100.0 * (double)attended - min_pct * (double)total) / min_pct + 1e-9);
    if (skips < 0) skips = 0;
    while (skips > 0 && calc_percentage(attended, total + skips) + 1e-6 < min_pct) {
        skips--;
    }
    while (calc_percentage(attended, total + skips + 1) + 1e-6 >= min_pct && skips < 5000) {
        skips++;
    }
    return skips;
}

static void print_student_json(const StudentRecord *s, const PolicyConfig *cfg) {
    double pct = calc_percentage(s->attended, s->total);
    const char *status = classify_status(pct, cfg);
    int needed = classes_needed_for_threshold(s->attended, s->total, cfg->min_threshold_pct);
    int skips = safe_skips_available(s->attended, s->total, cfg->min_threshold_pct);
    double deficit_pct = pct < cfg->min_threshold_pct ? (cfg->min_threshold_pct - pct) : 0.0;

    printf("{");
    printf("\"roll\":"); print_json_str(s->roll); printf(",");
    printf("\"name\":"); print_json_str(s->name); printf(",");
    printf("\"course\":"); print_json_str(s->course); printf(",");
    printf("\"semester\":%d,", s->semester);
    printf("\"email\":"); print_json_str(s->email); printf(",");
    printf("\"guardianEmail\":"); print_json_str(s->guardian_email); printf(",");
    printf("\"attended\":%d,", s->attended);
    printf("\"total\":%d,", s->total);
    printf("\"absent\":%d,", s->total - s->attended);
    printf("\"percentage\":%.2f,", pct);
    printf("\"status\":"); print_json_str(status); printf(",");
    printf("\"classesNeeded\":%d,", needed);
    printf("\"safeSkips\":%d,", skips);
    printf("\"deficitPct\":%.2f,", deficit_pct);
    printf("\"alertAcknowledged\":%s,", s->alert_acknowledged ? "true" : "false");
    printf("\"lastAlertDate\":"); print_json_str(s->last_alert_date);
    printf("}");
}

static void seed_demo_data(int force) {
    char path[512];
    build_path(path, sizeof(path), "students.bin");
    FILE *check = fopen(path, "rb");
    if (check && !force) {
        fclose(check);
        return;
    }
    if (check) fclose(check);

    PolicyConfig cfg;
    cfg.min_threshold_pct = 80.0;
    cfg.warning_threshold_pct = 85.0;
    save_config(&cfg);

    StudentRecord seed[] = {
        {"CS2401", "Aarav Mehta", "CS301", 5, "aarav.mehta@uni.edu", "r.mehta@guardian.org", 38, 42, 0, ""},
        {"CS2402", "Diya Nair", "CS301", 5, "diya.nair@uni.edu", "s.nair@guardian.org", 29, 42, 0, ""},
        {"CS2403", "Rohan Kulkarni", "CS301", 5, "rohan.k@uni.edu", "v.kulkarni@guardian.org", 33, 42, 0, ""},
        {"CS2404", "Sneha Iyer", "CS301", 5, "sneha.iyer@uni.edu", "m.iyer@guardian.org", 40, 42, 0, ""},
        {"CS2405", "Kabir Sharma", "CS301", 5, "kabir.s@uni.edu", "a.sharma@guardian.org", 25, 42, 1, "2026-11-16"},
        {"CS2406", "Ananya Verma", "CS301", 5, "ananya.v@uni.edu", "p.verma@guardian.org", 32, 42, 0, ""},
        {"EC2411", "Vikram Reddy", "EC204", 3, "vikram.r@uni.edu", "k.reddy@guardian.org", 36, 40, 0, ""},
        {"EC2412", "Meera Krishnan", "EC204", 3, "meera.k@uni.edu", "n.krishnan@guardian.org", 27, 40, 0, ""},
        {"EC2413", "Arjun Malhotra", "EC204", 3, "arjun.m@uni.edu", "d.malhotra@guardian.org", 31, 40, 0, ""},
        {"EC2414", "Priya Deshmukh", "EC204", 3, "priya.d@uni.edu", "s.deshmukh@guardian.org", 38, 40, 0, ""},
        {"EC2415", "Siddharth Joshi", "EC204", 3, "siddharth.j@uni.edu", "r.joshi@guardian.org", 24, 40, 0, ""},
        {"IT2421", "Neha Gupta", "IT402", 7, "neha.g@uni.edu", "a.gupta@guardian.org", 35, 38, 0, ""},
        {"IT2422", "Ishaan Chatterjee", "IT402", 7, "ishaan.c@uni.edu", "b.chatterjee@guardian.org", 26, 38, 0, ""},
        {"IT2423", "Tanvi Patil", "IT402", 7, "tanvi.p@uni.edu", "m.patil@guardian.org", 30, 38, 0, ""},
        {"IT2424", "Devansh Kapoor", "IT402", 7, "devansh.k@uni.edu", "n.kapoor@guardian.org", 29, 38, 0, ""},
        {"ME2431", "Karan Singh", "ME210", 3, "karan.s@uni.edu", "h.singh@guardian.org", 31, 36, 0, ""},
        {"ME2432", "Riya Banerjee", "ME210", 3, "riya.b@uni.edu", "t.banerjee@guardian.org", 23, 36, 1, "2026-11-17"},
        {"ME2433", "Aditya Rao", "ME210", 3, "aditya.r@uni.edu", "l.rao@guardian.org", 28, 36, 0, ""}
    };
    int seed_count = (int)(sizeof(seed) / sizeof(seed[0]));
    save_students(seed, seed_count);

    SessionEntry sessions[] = {
        {1, "CS301", "2026-11-18", "Pointers & Dynamic Memory Allocation", 4, 2, "CS2402,CS2405"},
        {2, "EC204", "2026-11-18", "Sequential Logic & Flip-Flop Timing", 3, 2, "EC2412,EC2415"},
        {3, "IT402", "2026-11-17", "Distributed Consensus & Raft", 3, 1, "IT2422"},
        {4, "ME210", "2026-11-17", "Thermodynamic Cycles & Entropy", 2, 1, "ME2432"},
        {5, "CS301", "2026-11-15", "File I/O & Binary Struct Serialization", 5, 1, "CS2405"}
    };
    int session_count = (int)(sizeof(sessions) / sizeof(sessions[0]));
    save_sessions(sessions, session_count);

    NotificationRecord notes[] = {
        {
            1,
            "CS2405",
            "Kabir Sharma",
            "CS301",
            "BOTH",
            "kabir.s@uni.edu, a.sharma@guardian.org",
            "Attendance Shortage Alert: CS2405 (59.52% < 80.0%)",
            "Dear Kabir Sharma, your attendance in CS301 is 59.52% (25/42), below the mandatory 80.0% requirement. Attend the next 43 consecutive classes to restore 80.0% eligibility.",
            "2026-11-16",
            "Portal Message Delivered",
            0
        },
        {
            2,
            "ME2432",
            "Riya Banerjee",
            "ME210",
            "MESSAGE",
            "riya.b@uni.edu",
            "Attendance Shortage Alert: ME210 (63.89% < 80.0%)",
            "Dear Riya Banerjee, your attendance in ME210 is 63.89% (23/36), below the mandatory 80.0% requirement. Attend the next 29 consecutive classes to regain 80.0% standing.",
            "2026-11-17",
            "Portal Message Delivered",
            0
        }
    };
    int note_count = (int)(sizeof(notes) / sizeof(notes[0]));
    save_notifications(notes, note_count);
}

static PolicyConfig g_sort_cfg;
static int g_sort_mode = 0; /* 0: PCT_ASC, 1: PCT_DESC, 2: ROLL, 3: NAME */

static int compare_students(const void *pa, const void *pb) {
    const StudentRecord *a = (const StudentRecord *)pa;
    const StudentRecord *b = (const StudentRecord *)pb;
    double pct_a = calc_percentage(a->attended, a->total);
    double pct_b = calc_percentage(b->attended, b->total);

    if (g_sort_mode == 0) {
        if (pct_a < pct_b) return -1;
        if (pct_a > pct_b) return 1;
        return strcmp(a->roll, b->roll);
    } else if (g_sort_mode == 1) {
        if (pct_a > pct_b) return -1;
        if (pct_a < pct_b) return 1;
        return strcmp(a->roll, b->roll);
    } else if (g_sort_mode == 3) {
        int c = strcmp(a->name, b->name);
        return c != 0 ? c : strcmp(a->roll, b->roll);
    }
    return strcmp(a->roll, b->roll);
}

static int cmd_list(int argc, char **argv) {
    const char *course_filter = "ALL";
    const char *status_filter = "ALL";
    const char *search_query = "";
    const char *sort_key = "PCT_ASC";

    for (int i = 2; i < argc; i++) {
        if (strncmp(argv[i], "--course=", 9) == 0) course_filter = argv[i] + 9;
        else if (strncmp(argv[i], "--status=", 9) == 0) status_filter = argv[i] + 9;
        else if (strncmp(argv[i], "--search=", 9) == 0) search_query = argv[i] + 9;
        else if (strncmp(argv[i], "--sort=", 7) == 0) sort_key = argv[i] + 7;
    }

    PolicyConfig cfg = load_config();
    StudentRecord *all = malloc(sizeof(StudentRecord) * MAX_STUDENTS);
    if (!all) return 1;
    int count = load_students(all, MAX_STUDENTS);

    if (strcmp(sort_key, "PCT_DESC") == 0) g_sort_mode = 1;
    else if (strcmp(sort_key, "ROLL") == 0) g_sort_mode = 2;
    else if (strcmp(sort_key, "NAME") == 0) g_sort_mode = 3;
    else g_sort_mode = 0;

    g_sort_cfg = cfg;
    if (count > 1) {
        qsort(all, (size_t)count, sizeof(StudentRecord), compare_students);
    }

    int safe_count = 0, warn_count = 0, crit_count = 0, unack_alerts = 0;
    double pct_sum = 0.0;

    typedef struct {
        char course[24];
        int students;
        int safe;
        int warning;
        int critical;
        double pct_sum;
    } CourseStat;

    CourseStat courses[64];
    int course_cnt = 0;

    for (int i = 0; i < count; i++) {
        double pct = calc_percentage(all[i].attended, all[i].total);
        pct_sum += pct;
        const char *st = classify_status(pct, &cfg);
        if (strcmp(st, "CRITICAL") == 0) {
            crit_count++;
            if (!all[i].alert_acknowledged) unack_alerts++;
        } else if (strcmp(st, "WARNING") == 0) {
            warn_count++;
            if (!all[i].alert_acknowledged) unack_alerts++;
        } else {
            safe_count++;
        }

        int cidx = -1;
        for (int c = 0; c < course_cnt; c++) {
            if (strcmp(courses[c].course, all[i].course) == 0) {
                cidx = c;
                break;
            }
        }
        if (cidx == -1 && course_cnt < 64) {
            cidx = course_cnt++;
            safe_copy(courses[cidx].course, sizeof(courses[cidx].course), all[i].course);
            courses[cidx].students = 0;
            courses[cidx].safe = 0;
            courses[cidx].warning = 0;
            courses[cidx].critical = 0;
            courses[cidx].pct_sum = 0.0;
        }
        if (cidx != -1) {
            courses[cidx].students++;
            courses[cidx].pct_sum += pct;
            if (strcmp(st, "CRITICAL") == 0) courses[cidx].critical++;
            else if (strcmp(st, "WARNING") == 0) courses[cidx].warning++;
            else courses[cidx].safe++;
        }
    }

    double avg_pct = count > 0 ? (pct_sum / (double)count) : 0.0;

    printf("{");
    printf("\"policy\":{\"minThreshold\":%.1f,\"warningThreshold\":%.1f},",
           cfg.min_threshold_pct, cfg.warning_threshold_pct);
    printf("\"summary\":{");
    printf("\"totalStudents\":%d,", count);
    printf("\"averageAttendance\":%.2f,", avg_pct);
    printf("\"safeCount\":%d,", safe_count);
    printf("\"warningCount\":%d,", warn_count);
    printf("\"criticalCount\":%d,", crit_count);
    printf("\"pendingAlerts\":%d", unack_alerts);
    printf("},");

    printf("\"courses\":[");
    for (int c = 0; c < course_cnt; c++) {
        if (c > 0) printf(",");
        double c_avg = courses[c].students > 0 ? courses[c].pct_sum / courses[c].students : 0.0;
        printf("{\"course\":"); print_json_str(courses[c].course);
        printf(",\"students\":%d,\"safe\":%d,\"warning\":%d,\"critical\":%d,\"avgPct\":%.2f}",
               courses[c].students, courses[c].safe, courses[c].warning, courses[c].critical, c_avg);
    }
    printf("],");

    printf("\"students\":[");
    int emitted = 0;
    for (int i = 0; i < count; i++) {
        double pct = calc_percentage(all[i].attended, all[i].total);
        const char *st = classify_status(pct, &cfg);

        if (strcmp(course_filter, "ALL") != 0 && !str_iequals(all[i].course, course_filter)) continue;
        if (strcmp(status_filter, "ALL") != 0 && !str_iequals(st, status_filter)) continue;
        if (search_query[0] != '\0') {
            if (!str_icontains(all[i].roll, search_query) &&
                !str_icontains(all[i].name, search_query) &&
                !str_icontains(all[i].course, search_query)) {
                continue;
            }
        }

        if (emitted > 0) printf(",");
        print_student_json(&all[i], &cfg);
        emitted++;
    }
    printf("]");
    printf("}\n");

    free(all);
    return 0;
}

static int cmd_add_student(int argc, char **argv) {
    if (argc < 10) {
        printf("{\"error\":\"Usage: add-student <roll> <name> <course> <semester> <email> <guardian_email> <attended> <total>\"}\n");
        return 1;
    }

    const char *roll = argv[2];
    const char *name = argv[3];
    const char *course = argv[4];
    int semester = atoi(argv[5]);
    const char *email = argv[6];
    const char *guardian_email = argv[7];
    int attended = atoi(argv[8]);
    int total = atoi(argv[9]);

    if (roll[0] == '\0' || name[0] == '\0' || course[0] == '\0') {
        printf("{\"error\":\"Roll, name, and course are required.\"}\n");
        return 1;
    }
    if (total < 0 || attended < 0 || attended > total) {
        printf("{\"error\":\"Attended classes must be between 0 and total classes.\"}\n");
        return 1;
    }
    if (semester < 1 || semester > 12) semester = 1;

    StudentRecord *all = malloc(sizeof(StudentRecord) * MAX_STUDENTS);
    if (!all) return 1;
    int count = load_students(all, MAX_STUDENTS);

    for (int i = 0; i < count; i++) {
        if (str_iequals(all[i].roll, roll)) {
            free(all);
            printf("{\"error\":\"A student with this roll number already exists.\"}\n");
            return 1;
        }
    }

    if (count >= MAX_STUDENTS) {
        free(all);
        printf("{\"error\":\"Maximum student capacity reached.\"}\n");
        return 1;
    }

    StudentRecord rec;
    memset(&rec, 0, sizeof(rec));
    safe_copy(rec.roll, sizeof(rec.roll), roll);
    safe_copy(rec.name, sizeof(rec.name), name);
    safe_copy(rec.course, sizeof(rec.course), course);
    rec.semester = semester;
    safe_copy(rec.email, sizeof(rec.email), email);
    safe_copy(rec.guardian_email, sizeof(rec.guardian_email), guardian_email);
    rec.attended = attended;
    rec.total = total;
    rec.alert_acknowledged = 0;
    rec.last_alert_date[0] = '\0';

    all[count++] = rec;
    if (!save_students(all, count)) {
        free(all);
        printf("{\"error\":\"Failed to persist student record to binary file.\"}\n");
        return 1;
    }

    PolicyConfig cfg = load_config();
    printf("{\"ok\":true,\"student\":");
    print_student_json(&rec, &cfg);
    printf("}\n");

    free(all);
    return 0;
}

static int cmd_update_record(int argc, char **argv) {
    if (argc < 5) {
        printf("{\"error\":\"Usage: update-record <roll> <attended> <total>\"}\n");
        return 1;
    }
    const char *roll = argv[2];
    int attended = atoi(argv[3]);
    int total = atoi(argv[4]);

    if (total < 0 || attended < 0 || attended > total) {
        printf("{\"error\":\"Invalid attendance counts.\"}\n");
        return 1;
    }

    StudentRecord *all = malloc(sizeof(StudentRecord) * MAX_STUDENTS);
    if (!all) return 1;
    int count = load_students(all, MAX_STUDENTS);

    int idx = -1;
    for (int i = 0; i < count; i++) {
        if (str_iequals(all[i].roll, roll)) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        free(all);
        printf("{\"error\":\"Student not found.\"}\n");
        return 1;
    }

    all[idx].attended = attended;
    all[idx].total = total;
    PolicyConfig cfg = load_config();
    double pct = calc_percentage(attended, total);
    if (pct >= cfg.warning_threshold_pct) {
        all[idx].alert_acknowledged = 0;
    }

    save_students(all, count);
    printf("{\"ok\":true,\"student\":");
    print_student_json(&all[idx], &cfg);
    printf("}\n");

    free(all);
    return 0;
}

static int cmd_update_contacts(int argc, char **argv) {
    if (argc < 5) {
        fputs("{\"error\":\"Usage: update-contacts <roll> <email> <guardian_email>\"}\n", stdout);
        return 1;
    }
    const char *roll = argv[2];
    const char *email = argv[3];
    const char *guardian_email = argv[4];

    StudentRecord *all = malloc(sizeof(StudentRecord) * MAX_STUDENTS);
    if (!all) return 1;
    int count = load_students(all, MAX_STUDENTS);

    int idx = -1;
    for (int i = 0; i < count; i++) {
        if (str_iequals(all[i].roll, roll)) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        free(all);
        fputs("{\"error\":\"Student not found.\"}\n", stdout);
        return 1;
    }

    safe_copy(all[idx].email, sizeof(all[idx].email), email);
    safe_copy(all[idx].guardian_email, sizeof(all[idx].guardian_email), guardian_email);
    save_students(all, count);

    PolicyConfig cfg = load_config();
    printf("{\"ok\":true,\"student\":");
    print_student_json(&all[idx], &cfg);
    printf("}\n");

    free(all);
    return 0;
}

static int cmd_delete_student(int argc, char **argv) {
    if (argc < 3) {
        printf("{\"error\":\"Usage: delete-student <roll>\"}\n");
        return 1;
    }
    const char *roll = argv[2];
    StudentRecord *all = malloc(sizeof(StudentRecord) * MAX_STUDENTS);
    if (!all) return 1;
    int count = load_students(all, MAX_STUDENTS);

    int found = 0;
    for (int i = 0; i < count; i++) {
        if (str_iequals(all[i].roll, roll)) {
            for (int j = i; j < count - 1; j++) {
                all[j] = all[j + 1];
            }
            count--;
            found = 1;
            break;
        }
    }
    if (!found) {
        free(all);
        printf("{\"error\":\"Student not found.\"}\n");
        return 1;
    }
    save_students(all, count);
    printf("{\"ok\":true,\"deleted\":");
    print_json_str(roll);
    printf("}\n");
    free(all);
    return 0;
}

static int csv_contains_roll(const char *csv, const char *roll) {
    if (!csv || csv[0] == '\0') return 0;
    char buf[2048];
    safe_copy(buf, sizeof(buf), csv);
    char *tok = strtok(buf, ",");
    while (tok) {
        while (*tok == ' ') tok++;
        if (str_iequals(tok, roll)) return 1;
        tok = strtok(NULL, ",");
    }
    return 0;
}

static int cmd_mark_session(int argc, char **argv) {
    if (argc < 7) {
        printf("{\"error\":\"Usage: mark-session <course> <date> <topic> <present_csv> <absent_csv>\"}\n");
        return 1;
    }
    const char *course = argv[2];
    const char *date = argv[3];
    const char *topic = argv[4];
    const char *present_csv = argv[5];
    const char *absent_csv = argv[6];

    StudentRecord *all = malloc(sizeof(StudentRecord) * MAX_STUDENTS);
    if (!all) return 1;
    int count = load_students(all, MAX_STUDENTS);

    int present_count = 0;
    int absent_count = 0;
    char recorded_absent[512];
    recorded_absent[0] = '\0';

    for (int i = 0; i < count; i++) {
        if (!str_iequals(all[i].course, course)) continue;
        if (csv_contains_roll(present_csv, all[i].roll)) {
            all[i].attended += 1;
            all[i].total += 1;
            present_count++;
        } else if (csv_contains_roll(absent_csv, all[i].roll)) {
            all[i].total += 1;
            all[i].alert_acknowledged = 0;
            absent_count++;
            if (strlen(recorded_absent) + strlen(all[i].roll) + 2 < sizeof(recorded_absent)) {
                if (recorded_absent[0] != '\0') strcat(recorded_absent, ",");
                strcat(recorded_absent, all[i].roll);
            }
        }
    }

    if (present_count == 0 && absent_count == 0) {
        free(all);
        printf("{\"error\":\"No matching students updated for this course.\"}\n");
        return 1;
    }

    save_students(all, count);

    SessionEntry *sessions = malloc(sizeof(SessionEntry) * MAX_SESSIONS);
    int scount = load_sessions(sessions, MAX_SESSIONS);
    int next_id = 1;
    for (int i = 0; i < scount; i++) {
        if (sessions[i].session_id >= next_id) next_id = sessions[i].session_id + 1;
    }
    if (scount < MAX_SESSIONS) {
        SessionEntry entry;
        memset(&entry, 0, sizeof(entry));
        entry.session_id = next_id;
        safe_copy(entry.course, sizeof(entry.course), course);
        safe_copy(entry.date, sizeof(entry.date), date);
        safe_copy(entry.topic, sizeof(entry.topic), topic);
        entry.present_count = present_count;
        entry.absent_count = absent_count;
        safe_copy(entry.absent_rolls, sizeof(entry.absent_rolls), recorded_absent);
        sessions[scount++] = entry;
        save_sessions(sessions, scount);
    }

    free(sessions);
    free(all);

    printf("{\"ok\":true,\"sessionId\":%d,\"presentCount\":%d,\"absentCount\":%d}\n",
           next_id, present_count, absent_count);
    return 0;
}

static int cmd_set_threshold(int argc, char **argv) {
    if (argc < 4) {
        printf("{\"error\":\"Usage: set-threshold <min_pct> <warn_pct>\"}\n");
        return 1;
    }
    double min_pct = atof(argv[2]);
    double warn_pct = atof(argv[3]);
    if (min_pct < 10.0 || min_pct > 95.0 || warn_pct <= min_pct || warn_pct > 99.0) {
        fputs("{\"error\":\"Minimum must be 10-95% and warning threshold must exceed minimum (up to 99%).\"}\n", stdout);
        return 1;
    }

    PolicyConfig cfg;
    cfg.min_threshold_pct = min_pct;
    cfg.warning_threshold_pct = warn_pct;
    if (!save_config(&cfg)) {
        printf("{\"error\":\"Failed to save policy configuration.\"}\n");
        return 1;
    }

    printf("{\"ok\":true,\"minThreshold\":%.1f,\"warningThreshold\":%.1f}\n",
           cfg.min_threshold_pct, cfg.warning_threshold_pct);
    return 0;
}

static int cmd_student_detail(int argc, char **argv) {
    if (argc < 3) {
        printf("{\"error\":\"Usage: student-detail <roll>\"}\n");
        return 1;
    }
    const char *roll = argv[2];
    PolicyConfig cfg = load_config();
    StudentRecord *all = malloc(sizeof(StudentRecord) * MAX_STUDENTS);
    if (!all) return 1;
    int count = load_students(all, MAX_STUDENTS);

    int idx = -1;
    for (int i = 0; i < count; i++) {
        if (str_iequals(all[i].roll, roll)) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        free(all);
        printf("{\"error\":\"Student not found.\"}\n");
        return 1;
    }

    SessionEntry *sessions = malloc(sizeof(SessionEntry) * MAX_SESSIONS);
    int scount = load_sessions(sessions, MAX_SESSIONS);

    printf("{\"policy\":{\"minThreshold\":%.1f,\"warningThreshold\":%.1f},\"student\":",
           cfg.min_threshold_pct, cfg.warning_threshold_pct);
    print_student_json(&all[idx], &cfg);
    printf(",\"recentSessions\":[");
    int emitted = 0;
    for (int i = scount - 1; i >= 0; i--) {
        if (!str_iequals(sessions[i].course, all[idx].course)) continue;
        if (emitted > 0) printf(",");
        int was_absent = csv_contains_roll(sessions[i].absent_rolls, all[idx].roll);
        printf("{\"sessionId\":%d,\"date\":", sessions[i].session_id);
        print_json_str(sessions[i].date);
        printf(",\"topic\":");
        print_json_str(sessions[i].topic);
        printf(",\"present\":%s}", was_absent ? "false" : "true");
        emitted++;
        if (emitted >= 12) break;
    }
    printf("]}\n");

    free(sessions);
    free(all);
    return 0;
}

static int cmd_alerts(void) {
    PolicyConfig cfg = load_config();
    StudentRecord *all = malloc(sizeof(StudentRecord) * MAX_STUDENTS);
    if (!all) return 1;
    int count = load_students(all, MAX_STUDENTS);

    g_sort_mode = 0; /* Sort lowest attendance first */
    g_sort_cfg = cfg;
    if (count > 1) {
        qsort(all, (size_t)count, sizeof(StudentRecord), compare_students);
    }

    printf("{\"policy\":{\"minThreshold\":%.1f,\"warningThreshold\":%.1f},\"alerts\":[",
           cfg.min_threshold_pct, cfg.warning_threshold_pct);
    int emitted = 0;
    for (int i = 0; i < count; i++) {
        double pct = calc_percentage(all[i].attended, all[i].total);
        const char *st = classify_status(pct, &cfg);
        if (strcmp(st, "SAFE") == 0) continue;
        if (emitted > 0) printf(",");
        print_student_json(&all[i], &cfg);
        emitted++;
    }
    printf("]}\n");
    free(all);
    return 0;
}

static int cmd_ack_alert(int argc, char **argv) {
    if (argc < 4) {
        printf("{\"error\":\"Usage: acknowledge-alert <roll> <date>\"}\n");
        return 1;
    }
    const char *roll = argv[2];
    const char *date = argv[3];

    StudentRecord *all = malloc(sizeof(StudentRecord) * MAX_STUDENTS);
    if (!all) return 1;
    int count = load_students(all, MAX_STUDENTS);

    int idx = -1;
    for (int i = 0; i < count; i++) {
        if (str_iequals(all[i].roll, roll)) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        free(all);
        printf("{\"error\":\"Student not found.\"}\n");
        return 1;
    }

    all[idx].alert_acknowledged = 1;
    safe_copy(all[idx].last_alert_date, sizeof(all[idx].last_alert_date), date);
    save_students(all, count);

    PolicyConfig cfg = load_config();
    printf("{\"ok\":true,\"student\":");
    print_student_json(&all[idx], &cfg);
    printf("}\n");
    free(all);
    return 0;
}

static int cmd_sessions(int argc, char **argv) {
    const char *course_filter = argc >= 3 ? argv[2] : "ALL";
    SessionEntry *sessions = malloc(sizeof(SessionEntry) * MAX_SESSIONS);
    int scount = load_sessions(sessions, MAX_SESSIONS);

    printf("{\"sessions\":[");
    int emitted = 0;
    for (int i = scount - 1; i >= 0; i--) {
        if (strcmp(course_filter, "ALL") != 0 && !str_iequals(sessions[i].course, course_filter)) continue;
        if (emitted > 0) printf(",");
        printf("{\"sessionId\":%d,\"course\":", sessions[i].session_id);
        print_json_str(sessions[i].course);
        printf(",\"date\":"); print_json_str(sessions[i].date);
        printf(",\"topic\":"); print_json_str(sessions[i].topic);
        printf(",\"presentCount\":%d,\"absentCount\":%d,\"absentRolls\":",
               sessions[i].present_count, sessions[i].absent_count);
        print_json_str(sessions[i].absent_rolls);
        printf("}");
        emitted++;
        if (emitted >= 50) break;
    }
    printf("]}\n");
    free(sessions);
    return 0;
}

static int cmd_export_csv(void) {
    PolicyConfig cfg = load_config();
    StudentRecord *all = malloc(sizeof(StudentRecord) * MAX_STUDENTS);
    if (!all) return 1;
    int count = load_students(all, MAX_STUDENTS);

    g_sort_mode = 0;
    qsort(all, (size_t)count, sizeof(StudentRecord), compare_students);

    printf("Roll,Name,Course,Semester,Attended,Total,Percentage,Status,ClassesNeeded,SafeSkips,Email,GuardianEmail,AlertStatus\n");
    for (int i = 0; i < count; i++) {
        double pct = calc_percentage(all[i].attended, all[i].total);
        const char *st = classify_status(pct, &cfg);
        int needed = classes_needed_for_threshold(all[i].attended, all[i].total, cfg.min_threshold_pct);
        int skips = safe_skips_available(all[i].attended, all[i].total, cfg.min_threshold_pct);
        printf("%s,\"%s\",%s,%d,%d,%d,%.2f,%s,%d,%d,%s,%s,%s\n",
               all[i].roll,
               all[i].name,
               all[i].course,
               all[i].semester,
               all[i].attended,
               all[i].total,
               pct,
               st,
               needed,
               skips,
               all[i].email,
               all[i].guardian_email,
               strcmp(st, "SAFE") == 0 ? "None" : (all[i].alert_acknowledged ? "Sent" : "Pending"));
    }
    free(all);
    return 0;
}

static void print_notification_json(const NotificationRecord *n) {
    printf("{");
    printf("\"notificationId\":%d,", n->notification_id);
    printf("\"roll\":"); print_json_str(n->roll); printf(",");
    printf("\"studentName\":"); print_json_str(n->student_name); printf(",");
    printf("\"course\":"); print_json_str(n->course); printf(",");
    printf("\"channel\":"); print_json_str(n->channel); printf(",");
    printf("\"recipient\":"); print_json_str(n->recipient); printf(",");
    printf("\"subject\":"); print_json_str(n->subject); printf(",");
    printf("\"message\":"); print_json_str(n->message); printf(",");
    printf("\"sentAt\":"); print_json_str(n->sent_at); printf(",");
    printf("\"deliveryStatus\":"); print_json_str(n->delivery_status); printf(",");
    printf("\"readByStudent\":%s", n->read_by_student ? "true" : "false");
    printf("}");
}

static int cmd_notify_student(int argc, char **argv) {
    if (argc < 9) {
        fputs("{\"error\":\"Usage: notify-student <roll> <channel> <recipient> <subject> <message> <sent_at> <delivery_status>\"}\n", stdout);
        return 1;
    }
    const char *roll = argv[2];
    const char *channel = argv[3];
    const char *recipient = argv[4];
    const char *subject = argv[5];
    const char *message = argv[6];
    const char *sent_at = argv[7];
    const char *delivery_status = argv[8];

    StudentRecord *all = malloc(sizeof(StudentRecord) * MAX_STUDENTS);
    if (!all) return 1;
    int count = load_students(all, MAX_STUDENTS);

    int idx = -1;
    for (int i = 0; i < count; i++) {
        if (str_iequals(all[i].roll, roll)) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        free(all);
        fputs("{\"error\":\"Student not found.\"}\n", stdout);
        return 1;
    }

    NotificationRecord *notes = malloc(sizeof(NotificationRecord) * MAX_NOTIFICATIONS);
    if (!notes) {
        free(all);
        return 1;
    }
    int ncount = load_notifications(notes, MAX_NOTIFICATIONS);
    int next_id = 1;
    for (int i = 0; i < ncount; i++) {
        if (notes[i].notification_id >= next_id) next_id = notes[i].notification_id + 1;
    }

    NotificationRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.notification_id = next_id;
    safe_copy(rec.roll, sizeof(rec.roll), all[idx].roll);
    safe_copy(rec.student_name, sizeof(rec.student_name), all[idx].name);
    safe_copy(rec.course, sizeof(rec.course), all[idx].course);
    safe_copy(rec.channel, sizeof(rec.channel), channel);
    safe_copy(rec.recipient, sizeof(rec.recipient), recipient);
    safe_copy(rec.subject, sizeof(rec.subject), subject);
    safe_copy(rec.message, sizeof(rec.message), message);
    safe_copy(rec.sent_at, sizeof(rec.sent_at), sent_at);
    safe_copy(rec.delivery_status, sizeof(rec.delivery_status), delivery_status);
    rec.read_by_student = 0;

    if (ncount < MAX_NOTIFICATIONS) {
        notes[ncount++] = rec;
        save_notifications(notes, ncount);
    }

    all[idx].alert_acknowledged = 1;
    safe_copy(all[idx].last_alert_date, sizeof(all[idx].last_alert_date), sent_at);
    save_students(all, count);

    printf("{\"ok\":true,\"notification\":");
    print_notification_json(&rec);
    printf("}\n");

    free(notes);
    free(all);
    return 0;
}

static int cmd_notify_defaulters(int argc, char **argv) {
    if (argc < 5) {
        fputs("{\"error\":\"Usage: notify-defaulters <channel> <sent_at> <delivery_status> [all|pending]\"}\n", stdout);
        return 1;
    }
    const char *channel = argv[2];
    const char *sent_at = argv[3];
    const char *delivery_status = argv[4];
    int only_pending = (argc >= 6 && strcmp(argv[5], "all") == 0) ? 0 : 1;

    PolicyConfig cfg = load_config();
    StudentRecord *all = malloc(sizeof(StudentRecord) * MAX_STUDENTS);
    NotificationRecord *notes = malloc(sizeof(NotificationRecord) * MAX_NOTIFICATIONS);
    if (!all || !notes) {
        free(all);
        free(notes);
        return 1;
    }

    int count = load_students(all, MAX_STUDENTS);
    int ncount = load_notifications(notes, MAX_NOTIFICATIONS);
    int next_id = 1;
    for (int i = 0; i < ncount; i++) {
        if (notes[i].notification_id >= next_id) next_id = notes[i].notification_id + 1;
    }

    int sent_count = 0;
    printf("{\"ok\":true,\"notified\":[");
    for (int i = 0; i < count; i++) {
        double pct = calc_percentage(all[i].attended, all[i].total);
        if (pct + 1e-6 >= cfg.min_threshold_pct) continue;
        if (only_pending && all[i].alert_acknowledged) continue;

        int needed = classes_needed_for_threshold(all[i].attended, all[i].total, cfg.min_threshold_pct);
        NotificationRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.notification_id = next_id++;
        safe_copy(rec.roll, sizeof(rec.roll), all[i].roll);
        safe_copy(rec.student_name, sizeof(rec.student_name), all[i].name);
        safe_copy(rec.course, sizeof(rec.course), all[i].course);
        safe_copy(rec.channel, sizeof(rec.channel), channel);
        snprintf(rec.recipient, sizeof(rec.recipient), "%s, %s", all[i].email, all[i].guardian_email);
        snprintf(rec.subject, sizeof(rec.subject),
                 "Attendance Shortage Alert: %s (%.2f%% < %.1f%%)",
                 all[i].roll, pct, cfg.min_threshold_pct);
        snprintf(rec.message, sizeof(rec.message),
                 "Dear %s, your attendance in %s is %.2f%% (%d/%d), below the mandatory %.1f%% requirement. Attend the next %d consecutive classes to restore %.1f%% eligibility.",
                 all[i].name, all[i].course, pct, all[i].attended, all[i].total,
                 cfg.min_threshold_pct, needed, cfg.min_threshold_pct);
        safe_copy(rec.sent_at, sizeof(rec.sent_at), sent_at);
        safe_copy(rec.delivery_status, sizeof(rec.delivery_status), delivery_status);
        rec.read_by_student = 0;

        if (ncount < MAX_NOTIFICATIONS) {
            notes[ncount++] = rec;
        }
        all[i].alert_acknowledged = 1;
        safe_copy(all[i].last_alert_date, sizeof(all[i].last_alert_date), sent_at);

        if (sent_count > 0) printf(",");
        print_notification_json(&rec);
        sent_count++;
    }
    printf("],\"count\":%d}\n", sent_count);

    save_notifications(notes, ncount);
    save_students(all, count);
    free(notes);
    free(all);
    return 0;
}

static int cmd_notifications(int argc, char **argv) {
    const char *roll_filter = argc >= 3 ? argv[2] : "ALL";
    NotificationRecord *notes = malloc(sizeof(NotificationRecord) * MAX_NOTIFICATIONS);
    if (!notes) return 1;
    int ncount = load_notifications(notes, MAX_NOTIFICATIONS);

    int unread = 0;
    for (int i = 0; i < ncount; i++) {
        if (strcmp(roll_filter, "ALL") != 0 && !str_iequals(notes[i].roll, roll_filter)) continue;
        if (!notes[i].read_by_student) unread++;
    }

    printf("{\"unreadCount\":%d,\"notifications\":[", unread);
    int emitted = 0;
    for (int i = ncount - 1; i >= 0; i--) {
        if (strcmp(roll_filter, "ALL") != 0 && !str_iequals(notes[i].roll, roll_filter)) continue;
        if (emitted > 0) printf(",");
        print_notification_json(&notes[i]);
        emitted++;
        if (emitted >= 100) break;
    }
    printf("]}\n");
    free(notes);
    return 0;
}

static int cmd_mark_notification_read(int argc, char **argv) {
    if (argc < 3) {
        fputs("{\"error\":\"Usage: mark-notification-read <id>\"}\n", stdout);
        return 1;
    }
    int target_id = atoi(argv[2]);
    NotificationRecord *notes = malloc(sizeof(NotificationRecord) * MAX_NOTIFICATIONS);
    if (!notes) return 1;
    int ncount = load_notifications(notes, MAX_NOTIFICATIONS);

    int found = 0;
    for (int i = 0; i < ncount; i++) {
        if (notes[i].notification_id == target_id) {
            notes[i].read_by_student = 1;
            found = 1;
            break;
        }
    }
    if (!found) {
        free(notes);
        fputs("{\"error\":\"Notification not found.\"}\n", stdout);
        return 1;
    }
    save_notifications(notes, ncount);
    printf("{\"ok\":true,\"notificationId\":%d}\n", target_id);
    free(notes);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fputs("{\"error\":\"Missing command. Supported: init, list, add-student, update-record, delete-student, mark-session, set-threshold, student-detail, alerts, acknowledge-alert, notify-student, notify-defaulters, notifications, mark-notification-read, sessions, export-csv, reset-demo\"}\n", stdout);
        return 1;
    }

    const char *cmd = argv[1];
    seed_demo_data(0);

    if (strcmp(cmd, "init") == 0) {
        printf("{\"ok\":true,\"message\":\"Attendance engine initialized.\"}\n");
        return 0;
    } else if (strcmp(cmd, "reset-demo") == 0) {
        seed_demo_data(1);
        printf("{\"ok\":true,\"message\":\"Dataset reset to initial semester state.\"}\n");
        return 0;
    } else if (strcmp(cmd, "list") == 0) {
        return cmd_list(argc, argv);
    } else if (strcmp(cmd, "add-student") == 0) {
        return cmd_add_student(argc, argv);
    } else if (strcmp(cmd, "update-record") == 0) {
        return cmd_update_record(argc, argv);
    } else if (strcmp(cmd, "update-contacts") == 0) {
        return cmd_update_contacts(argc, argv);
    } else if (strcmp(cmd, "delete-student") == 0) {
        return cmd_delete_student(argc, argv);
    } else if (strcmp(cmd, "mark-session") == 0) {
        return cmd_mark_session(argc, argv);
    } else if (strcmp(cmd, "set-threshold") == 0) {
        return cmd_set_threshold(argc, argv);
    } else if (strcmp(cmd, "student-detail") == 0) {
        return cmd_student_detail(argc, argv);
    } else if (strcmp(cmd, "alerts") == 0) {
        return cmd_alerts();
    } else if (strcmp(cmd, "acknowledge-alert") == 0) {
        return cmd_ack_alert(argc, argv);
    } else if (strcmp(cmd, "notify-student") == 0) {
        return cmd_notify_student(argc, argv);
    } else if (strcmp(cmd, "notify-defaulters") == 0) {
        return cmd_notify_defaulters(argc, argv);
    } else if (strcmp(cmd, "notifications") == 0) {
        return cmd_notifications(argc, argv);
    } else if (strcmp(cmd, "mark-notification-read") == 0) {
        return cmd_mark_notification_read(argc, argv);
    } else if (strcmp(cmd, "sessions") == 0) {
        return cmd_sessions(argc, argv);
    } else if (strcmp(cmd, "export-csv") == 0) {
        return cmd_export_csv();
    }

    printf("{\"error\":\"Unknown command.\"}\n");
    return 1;
}
