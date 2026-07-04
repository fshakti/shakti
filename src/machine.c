#include "machine.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/utsname.h>
#include <unistd.h>
#endif
#if defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include <sysinfoapi.h>
#endif

#define MACHINE_BENCH_BYTES (4 * 1024 * 1024)

static const char *use_bench_dir = "/tmp";

static void mput(V *d, const char *k, V *v);
static char*rtrim(char*a){char *e=a+strlen(a),oa=*a;if(!*a)return a;*a='*';while(e[-1]==' ')*--e=0;*a=oa;return a;}

static void fmt_bytes(char *out, size_t outsz, int64_t bytes) {
    static const char *u[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double v = (double)bytes;
    int ui = 0;
    while (v >= 1024.0 && ui < 5) {
        v /= 1024.0;
        ui++;
    }
    if (ui == 0)
        snprintf(out, outsz, "%lld B", (long long)bytes);
    else
        snprintf(out, outsz, "%.2f %s", v, u[ui]);
}

static void fmt_kb(char *out, size_t outsz, int64_t kb) {
    fmt_bytes(out, outsz, kb * 1024);
}

static void fmt_freq_mhz(char *out, size_t outsz, double mhz) {
    if (mhz >= 1000.0)
        snprintf(out, outsz, "%.2f GHz", mhz / 1000.0);
    else if (mhz > 0.0)
        snprintf(out, outsz, "%.0f MHz", mhz);
    else
        snprintf(out, outsz, "unknown");
}

static void fmt_throughput(char *out, size_t outsz, double mbps) {
    if (mbps >= 1024.0)
        snprintf(out, outsz, "%.2f GB/s", mbps / 1024.0);
    else
        snprintf(out, outsz, "%.2f MB/s", mbps);
}

static void mput(V *d, const char *k, V *v) {
    v_dict_set(d, k, v);
    v_free(v);
}

static V *mdict(void) { return v_dict(v_list(0), v_list(0)); }

static int read_text(const char *path, char *buf, size_t bufsz) {
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    if (!fgets(buf, (int)bufsz, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = 0;
    return 0;
}

static int64_t read_int_file(const char *path) {
    char buf[64];
    if (read_text(path, buf, sizeof buf) != 0)
        return -1;
    return (int64_t)strtoll(buf, NULL, 10);
}

static double mono_secs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int bench_rw(const char *dir, double *read_mbps, double *write_mbps) {
    char tmpl[512];
    snprintf(tmpl, sizeof tmpl, "%s/.shakti_bench_XXXXXX", dir);
#if defined(__linux__) || defined(__APPLE__)
    int fd = mkstemp(tmpl);
#else
    int fd = -1;
#endif
    if (fd < 0)
        return -1;

    size_t sz = MACHINE_BENCH_BYTES;
    char *buf = (char *)malloc(sz);
    if (!buf) {
        close(fd);
        unlink(tmpl);
        return -1;
    }
    memset(buf, 0xA5, sz);

    double t0 = mono_secs();
    size_t off = 0;
    while (off < sz) {
        ssize_t n = write(fd, buf + off, sz - off);
        if (n <= 0) {
            free(buf);
            close(fd);
            unlink(tmpl);
            return -1;
        }
        off += (size_t)n;
    }
#if defined(__linux__) || defined(__APPLE__)
    fsync(fd);
#endif
    double tw = mono_secs() - t0;
    *write_mbps = tw > 0 ? (sz / (1024.0 * 1024.0)) / tw : 0.0;

#if defined(__linux__) || defined(__APPLE__)
    if (lseek(fd, 0, SEEK_SET) < 0) {
        free(buf);
        close(fd);
        unlink(tmpl);
        return -1;
    }
#endif
    volatile unsigned char sink = 0;
    t0 = mono_secs();
    off = 0;
    while (off < sz) {
        ssize_t n = read(fd, buf + off, sz - off);
        if (n <= 0)
            break;
        sink ^= buf[off];
        off += (size_t)n;
    }
    (void)sink;
    double tr = mono_secs() - t0;
    *read_mbps = tr > 0 ? (sz / (1024.0 * 1024.0)) / tr : 0.0;

    free(buf);
    close(fd);
    unlink(tmpl);
    return 0;
}
static void fill_id(V *root) {
#if defined(_Win32)
    HANDLE h=CreateFile("C:\\", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL));
    STORAGE_PROPERTY_QUERY q={};
    q.PropertyId = StorageDeviceProperty;q.QueryType = PropertyStandardQuery;
    DWORD br;
    STORAGE_DESCRIPTOR_HEADER hdr={};
    if(!DeviceIoControl(h,IOCTL_STORAGE_QUERY_PROPERTY,&q,sizeof(q),&hdr,sizeof(hdr),&br,NULL))return;
    union{ char buf[hdr.Size];STORAGE_DEVICE_DESCRIPTOR p; }a;
    if(!DeviceIoControl(h,IOCTL_STORAGE_QUERY_PROPERTY,&q,sizeof(q),a.buf,sizeof(a),&br,NULL))return;
    if(a.p->SerialNumberOffset >= sizeof(a))return;
    for(int i=p->SerialNumberOffset;i<sizeof(a);++i)if(!a.buf[i])mput(root,"id",v_str(a.buf+a.p->SerialNumberOffset));
#elif defined(__linux__)
    char buffer[128];if(!read_text("/etc/machine-id",buffer,sizeof(buffer)-1))mput(root,"id",v_str(buffer));
#elif defined(__APPLE__)
    FILE*p=popen("ioreg -c IOPlatformExpertDevice -d 2 | awk -F \\\" '/IOPlatformSerialNumber/{print $(NF-1)}'","r");
    char buffer[128];buffer[sizeof(buffer)-1]=0;*buffer=0;
    if(fgets(buffer,sizeof(buffer)-1,p)&&*buffer)mput(root,"id",v_str(buffer));pclose(p);
#endif
}

static void fill_os(V *root) {
    V *os = mdict();
#if defined(_WIN32)
    const char *e=getenv("COMPUTERNAME");
    if(e)mput(os,"host",v_str(e));
#else
    const char *e=getenv("HOSTNAME");
    if(e)mput(os,"host",v_str(e));
#endif

#if defined(__linux__) || defined(__APPLE__)
    struct utsname uts;
    if (uname(&uts) == 0) {
        mput(os, "name", v_str(uts.sysname));
        mput(os, "release", v_str(uts.release));
        mput(os, "version", v_str(uts.version));
        mput(os, "arch", v_str(uts.machine));
    }
    char host[256];
    if (gethostname(host, sizeof host) == 0)
        mput(os, "hostname", v_str(host));
#elif defined(_WIN32)
    mput(os, "name", v_str("Windows"));
    OSVERSIONINFOA k={0};k.dwOSVersionInfoSize=sizeof(k);GetVersionExA(&k);
    char version[16];snprintf(version,sizeof(version),"%d.%d (%d)",k.dwMajorVersion,k.dwMinorVersion,k.dwBuildNumber);
    mput(os, "version", v_str(version));if(*k.szCSDVersion)mput(os,"release",v_str(k.szCSDVersion));
    char host[256];DWORD hn=sizeof(host)-1;
    if(GetComputerNameA(host,&hn))host[hn]=0,mput(os,"hostname",v_str(host));
#else
    mput(os, "name", "unknown");
#endif
    mput(root, "os", os);
}

#if defined(__linux__)

static void linux_cpu_model(char *model, size_t modelsz) {
    model[0] = 0;
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f)
        return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "model name", 10) || !strncmp(line, "Model", 5)) {
            char *colon = strchr(line, ':');
            if (colon) {
                colon++;
                while (*colon == ' ' || *colon == '\t')
                    colon++;
                snprintf(model, modelsz, "%s", colon);
                size_t n = strlen(model);
                while (n && (model[n - 1] == '\n' || model[n - 1] == '\r'))
                    model[--n] = 0;
                break;
            }
        }
    }
    fclose(f);
}

static int linux_count_cpuinfo_key(const char *key) {
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f)
        return 0;
    char line[256];
    int n = 0;
    size_t klen = strlen(key);
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, key, klen) && line[klen] == ':')
            n++;
    }
    fclose(f);
    return n;
}

static int linux_ccd_count(void) {
    DIR *d = opendir("/sys/devices/system/cpu");
    if (!d)
        return 1;
    int dies[256];
    int nd = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "cpu", 3) != 0)
            continue;
        const char *num = de->d_name + 3;
        if (!*num || !(*num >= '0' && *num <= '9'))
            continue;
        char path[256];
        snprintf(path, sizeof path,
                 "/sys/devices/system/cpu/%s/topology/die_id", de->d_name);
        int64_t die = read_int_file(path);
        if (die < 0)
            continue;
        int found = 0;
        for (int i = 0; i < nd; i++) {
            if (dies[i] == (int)die) {
                found = 1;
                break;
            }
        }
        if (!found && nd < 256)
            dies[nd++] = (int)die;
    }
    closedir(d);
    if (nd > 0)
        return nd;
    /* Fallback: count distinct L3 cache instances. */
    nd = 0;
    char seen[64][64];
    int nseen = 0;
    d = opendir("/sys/devices/system/cpu/cpu0/cache");
    if (!d)
        return 1;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        char lvl_path[320];
        snprintf(lvl_path, sizeof lvl_path,
                 "/sys/devices/system/cpu/cpu0/cache/%s/level", de->d_name);
        if (read_int_file(lvl_path) != 3)
            continue;
        char shared_path[320];
        snprintf(shared_path, sizeof shared_path,
                 "/sys/devices/system/cpu/cpu0/cache/%s/shared_cpu_list", de->d_name);
        char shared[64];
        if (read_text(shared_path, shared, sizeof shared) != 0)
            continue;
        int dup = 0;
        for (int i = 0; i < nseen; i++) {
            if (!strcmp(seen[i], shared)) {
                dup = 1;
                break;
            }
        }
        if (!dup && nseen < 64) {
            snprintf(seen[nseen++], sizeof seen[0], "%s", shared);
            nd++;
        }
    }
    closedir(d);
    return nd > 0 ? nd : 1;
}

static void linux_cpu_cache(V *cpu) {
    int64_t l1d = 0, l1i = 0, l2 = 0, l3 = 0;
    DIR *d = opendir("/sys/devices/system/cpu/cpu0/cache");
    if (!d)
        return;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        char base[320];
        snprintf(base, sizeof base, "/sys/devices/system/cpu/cpu0/cache/%s", de->d_name);
        char path[384];
        snprintf(path, sizeof path, "%s/level", base);
        int64_t level = read_int_file(path);
        snprintf(path, sizeof path, "%s/type", base);
        char typ[32];
        if (read_text(path, typ, sizeof typ) != 0)
            continue;
        snprintf(path, sizeof path, "%s/size", base);
        char szbuf[32];
        if (read_text(path, szbuf, sizeof szbuf) != 0)
            continue;
        int64_t kb = (int64_t)strtoll(szbuf, NULL, 10);
        if (level == 1 && !strcmp(typ, "Data"))
            l1d += kb;
        else if (level == 1 && !strcmp(typ, "Instruction"))
            l1i += kb;
        else if (level == 2)
            l2 += kb;
        else if (level == 3)
            l3 += kb;
    }
    closedir(d);
    mput(cpu, "l1d", v_int(l1d));
    mput(cpu, "l1i", v_int(l1i));
    mput(cpu, "l2", v_int(l2));
    mput(cpu, "l3", v_int(l3));
}

static void linux_cpu_clock(V *cpu) {
    int64_t cur_khz = read_int_file(
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    int64_t max_khz = read_int_file(
        "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq");
    if (cur_khz > 0)
        mput(cpu, "clock", v_float( (double)cur_khz / 1000.0) );
    if (max_khz > 0)
        mput(cpu, "clock_max", v_float( (double)max_khz / 1000.0) );
    if (cur_khz > 0 || max_khz > 0)
        return;
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f)
        return;
    char line[256];
    double mhz = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "cpu MHz", 7)) {
            char *colon = strchr(line, ':');
            if (colon)
                mhz = strtod(colon + 1, NULL);
            break;
        }
    }
    fclose(f);
    if (mhz > 0)
        mput(cpu, "clock", v_float(mhz));
}

static void fill_cpu(V *root) {
    V *cpu = mdict();
    char model[256];
    linux_cpu_model(model, sizeof model);
    if (model[0])
        mput(cpu, "model", v_str(rtrim(model)));
    int threads = linux_count_cpuinfo_key("processor");
    int cores = linux_count_cpuinfo_key("cpu cores");
    int sockets = linux_count_cpuinfo_key("physical id");
    if (threads <= 0)
        threads = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (cores <= 0)
        cores = threads;
    if (sockets <= 0)
        sockets = 1;
    mput(cpu, "threads", v_int(threads));
    mput(cpu, "cores", v_int(cores));
    mput(cpu, "sockets", v_int(sockets));
    mput(cpu, "ccd", v_int(linux_ccd_count()));
    linux_cpu_cache(cpu);
    linux_cpu_clock(cpu);
    mput(root, "cpu", cpu);
}

static void fill_ram(V *root) {
    struct sysinfo si;
    V *ram = mdict();
    if (sysinfo(&si) == 0) {
        mput(ram, "total", v_int( (int64_t)si.totalram * (int64_t)si.mem_unit) );
        mput(ram, "free", v_int( (int64_t)si.freeram * (int64_t)si.mem_unit) );
        mput(ram, "available", v_int( (int64_t)(si.freeram + si.bufferram) * (int64_t)si.mem_unit) );
        mput(ram, "swap", v_int( (int64_t)si.totalswap * (int64_t)si.mem_unit ));
    }
    mput(root, "ram", ram);
}

static const char *linux_disk_type(const char *dev, int rotational) {
    if (!strncmp(dev, "nvme", 4))
        return "nvme";
    if (!strncmp(dev, "mmcblk", 6))
        return "mmc";
    if (!strncmp(dev, "sd", 2) || !strncmp(dev, "vd", 2))
        return rotational ? "hdd" : "ssd";
    return rotational ? "hdd" : "block";
}

static void linux_disks(V *root) {
    V *disks = v_list(0);
    double bench_rd = 0, bench_wr = 0;
    int have_bench = bench_rw(use_bench_dir, &bench_rd, &bench_wr);
    DIR *d = opendir("/sys/block");
    if (!d) {
        mput(root, "disks", disks);
        return;
    }
    struct dirent *de;
    int first = 1;
    while ((de = readdir(d))) {
        const char *name = de->d_name;
        if (name[0] == '.')
            continue;
        if (!strncmp(name, "loop", 4) || !strncmp(name, "ram", 3) ||
            !strncmp(name, "dm-", 3) || !strncmp(name, "sr", 2))
            continue;
        char path[384];
        snprintf(path, sizeof path, "/sys/block/%s/size", name);
        int64_t sectors = read_int_file(path);
        if (sectors < 0)
            continue;
        snprintf(path, sizeof path, "/sys/block/%s/queue/rotational", name);
        int rotational = (int)read_int_file(path);
        if (rotational < 0)
            rotational = 0;

        V *disk = mdict();
        mput(disk, "device", v_str(name));
        mput(disk, "size", v_int(sectors * 512));
        mput(disk, "type", v_str(linux_disk_type(name, rotational)));
        mput(disk, "rotational", v_bool(rotational != 0));

        snprintf(path, sizeof path, "/sys/block/%s/device/model", name);
        char model[128];
        if (read_text(path, model, sizeof model) == 0)
            mput(disk, "model", v_str(rtrim(model)));
        else {
            snprintf(path, sizeof path, "/sys/block/%s/device/name", name);
            if (read_text(path, model, sizeof model) == 0)
                mput(disk, "model", v_str(rtrim(model)));
        }

        if (have_bench == 0 && first) {
            mput(disk, "read", v_float(bench_rd));
            mput(disk, "write", v_float(bench_wr));
            mput(disk, "bench_path", v_str(use_bench_dir));
            first = 0;
        }
        v_list_append(disks, disk);
        v_free(disk);
    }
    closedir(d);
    mput(root, "disks", disks);
}

static void linux_gpu_clock(const char *card, V *gpu) {
    char path[384];
    snprintf(path, sizeof path, "/sys/class/drm/%s/device/gt_cur_freq_mhz", card);
    int64_t cur = read_int_file(path);
    snprintf(path, sizeof path, "/sys/class/drm/%s/device/gt_max_freq_mhz", card);
    int64_t max = read_int_file(path);
    if (cur > 0)
        mput(gpu, "clock", v_float( (double)cur) );
    if (max > 0)
        mput(gpu, "clock_max", v_float( (double)max) );
    if (cur > 0 || max > 0)
        return;

    snprintf(path, sizeof path, "/sys/class/drm/%s/device/current_gpu_freq_mhz", card);
    cur = read_int_file(path);
    if (cur > 0) {
        mput(gpu, "clock", v_float( (double)cur) );
        return;
    }

    snprintf(path, sizeof path, "/sys/class/drm/%s/device/pp_dpm_sclk", card);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[128];
    double active = 0, peak = 0;
    while (fgets(line, sizeof line, f)) {
        char *mhzpos = strstr(line, "Mhz");
        if (!mhzpos)
            mhzpos = strstr(line, "MHz");
        if (!mhzpos)
            continue;
        char *p = line;
        while (*p && (*p < '0' || *p > '9'))
            p++;
        double mhz = strtod(p, NULL);
        if (mhz > peak)
            peak = mhz;
        if (strchr(line, '*'))
            active = mhz;
    }
    fclose(f);
    if (active > 0)
        mput(gpu, "clock", v_float( active) );
    if (peak > 0)
        mput(gpu, "clock_max", v_float(peak));
}

static void linux_gpu_one(const char *card, V *gpus) {
    char path[384];
    snprintf(path, sizeof path, "/sys/class/drm/%s/device/vendor", card);
    char vendor[32];
    read_text(path, vendor, sizeof vendor);
    snprintf(path, sizeof path, "/sys/class/drm/%s/device/device", card);
    char device[32];
    read_text(path, device, sizeof device);

    snprintf(path, sizeof path, "/sys/class/drm/%s/device/product_name", card);
    char product[128];
    int has_product = read_text(path, product, sizeof product) == 0;
    if (!has_product) {
        snprintf(path, sizeof path, "/sys/class/drm/%s/device/name", card);
        has_product = read_text(path, product, sizeof product) == 0;
    }

    V *gpu = mdict();
    if (has_product)
        mput(gpu, "name", product);
    else
        mput(gpu, "name", card);
    if (vendor[0])
        mput(gpu, "vendor_id", vendor);
    if (device[0])
        mput(gpu, "device_id", device);

    int64_t vram = -1;
    snprintf(path, sizeof path, "/sys/class/drm/%s/device/mem_info_vram_total", card);
    vram = read_int_file(path);
    if (vram < 0) {
        snprintf(path, sizeof path, "/sys/class/drm/%s/device/mem_info_vis_vram_total", card);
        vram = read_int_file(path);
    }
    if (vram >= 0)
        mput(gpu, "vram", v_int(vram));

    snprintf(path, sizeof path, "/sys/class/drm/%s/device/gpu_busy_percent", card);
    (void)path;

    snprintf(path, sizeof path, "/sys/class/drm/%s/device/gfx_cu_count", card);
    int64_t cu = read_int_file(path);
    mput(gpu, "cores", v_int(cu >= 0 ? cu : 0));

    linux_gpu_clock(card, gpu);

    v_list_append(gpus, gpu);
    v_free(gpu);
}

static void fill_gpus(V *root) {
    V *gpus = v_list(0);
    DIR *d = opendir("/sys/class/drm");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            const char *n = de->d_name;
            if (strncmp(n, "card", 4) != 0)
                continue;
            size_t len = strlen(n);
            if (len > 4) {
                for (size_t i = 4; i < len; i++) {
                    if (n[i] < '0' || n[i] > '9')
                        goto next_drm;
                }
            }
            char path[256];
            snprintf(path, sizeof path, "/sys/class/drm/%s/device", n);
            struct stat sbuf;
            if (stat(path, &sbuf) != 0 || !S_ISDIR(sbuf.st_mode))
                continue;
            linux_gpu_one(n, gpus);
        next_drm:;
        }
        closedir(d);
    }
    /* NVIDIA proprietary driver. */
    DIR *nd = opendir("/proc/driver/nvidia/gpus");
    if (nd) {
        struct dirent *de;
        while ((de = readdir(nd))) {
            if (de->d_name[0] == '.')
                continue;
            char info_path[320];
            snprintf(info_path, sizeof info_path,
                     "/proc/driver/nvidia/gpus/%s/information", de->d_name);
            FILE *f = fopen(info_path, "r");
            if (!f)
                continue;
            V *gpu = mdict();
            mput(gpu, "name", v_str("NVIDIA GPU"));
            char line[256];
            while (fgets(line, sizeof line, f)) {
                if (!strncmp(line, "Model:", 6)) {
                    char *v = strchr(line, ':');
                    if (v) {
                        v++;
                        while (*v == ' ')
                            v++;
                        size_t n = strlen(v);
                        while (n && (v[n - 1] == '\n' || v[n - 1] == '\r'))
                            v[--n] = 0;
                        mput(gpu, "name", v_str(v));
                    }
                } else if (!strncmp(line, "Total Memory:", 13)) {
                    char *v = strchr(line, ':');
                    if (v) {
                        v++;
                        while (*v == ' ')
                            v++;
                        int64_t mib = (int64_t)strtoll(v, NULL, 10);
                        mput(gpu, "vram", v_int(mib * 1024 * 1024));
                    }
                }
            }
            fclose(f);
            mput(gpu, "cores", v_int(0));
            v_list_append(gpus, gpu);
            v_free(gpu);
        }
        closedir(nd);
    }
    mput(root, "gpu", gpus);
}

#elif defined(__APPLE__)

static void sysctl_str(const char *name, char *buf, size_t bufsz) {
    size_t len = bufsz;
    if (sysctlbyname(name, buf, &len, NULL, 0) != 0)
        buf[0] = 0;
    else
        buf[len < bufsz ? len : bufsz - 1] = 0;
}

static int64_t sysctl_i64(const char *name) {
    int64_t v = 0;
    size_t len = sizeof v;
    if (sysctlbyname(name, &v, &len, NULL, 0) != 0)
        return -1;
    return v;
}
static void fill_cpu(V *root) {
    V *cpu = mdict();
    char buf[256];
    sysctl_str("machdep.cpu.brand_string", buf, sizeof buf);
    if (buf[0])
        mput(cpu, "model", v_str(rtrim(buf)));
    int64_t threads = sysctl_i64("hw.logicalcpu");
    int64_t cores = sysctl_i64("hw.physicalcpu");
    int64_t packages = sysctl_i64("hw.packages");
    if (threads < 0)
        threads = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores < 0)
        cores = threads;
    if (packages < 0)
        packages = 1;
    mput(cpu, "threads", v_int(threads));
    mput(cpu, "cores", v_int(cores));
    mput(cpu, "sockets", v_int(packages));
    mput(cpu, "ccd", v_int(packages > 0 ? packages : 1));
    int64_t l1d = sysctl_i64("hw.l1dcachesize");
    int64_t l1i = sysctl_i64("hw.l1icachesize");
    int64_t l2 = sysctl_i64("hw.l2cachesize");
    int64_t l3 = sysctl_i64("hw.l3cachesize");
    if (l1d >= 0)
        mput(cpu, "l1d", v_int(l1d));
    if (l1i >= 0)
        mput(cpu, "l1i", v_int(l1i));
    if (l2 >= 0)
        mput(cpu, "l2", v_int(l2));
    if (l3 >= 0)
        mput(cpu, "l3", v_int(l3));
    int64_t freq = sysctl_i64("hw.cpufrequency");
    if (freq > 0)
        mput(cpu, "clock", v_float( (double)freq / 1000000.0) );
    freq = sysctl_i64("hw.cpufrequency_max");
    if (freq > 0)
        mput(cpu, "clock_max", v_float( (double)freq / 1000000.0) );
    mput(root, "cpu", cpu);
}

static void fill_ram(V *root) {
    V *ram = mdict();
    int64_t mem = sysctl_i64("hw.memsize");
    if (mem >= 0)
        mput(ram, "total", v_int(mem));
    vm_size_t page_free = 0;
    mach_port_t port = mach_host_self();
    vm_statistics64_data_t vmstat;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(port, HOST_VM_INFO64, (host_info64_t)&vmstat, &count) == KERN_SUCCESS) {
        page_free = vmstat.free_count;
        mput(ram, "free", v_int( (int64_t)page_free * (int64_t)vm_page_size) );
    }
    mput(root, "ram", ram);
}

static void fill_gpus(V *root) {
    V *gpus = v_list(0);
    V *gpu = mdict();
    char model[128];
    sysctl_str("machdep.gpu.model", model, sizeof model);
    if (!model[0])
        snprintf(model, sizeof model, "Apple GPU");
    mput(gpu, "name", v_str(model));
    int64_t mem = sysctl_i64("hw.memsize");
    if (mem >= 0) {
        mput(gpu, "unified_memory", v_bool(1));
        mput(gpu, "system_ram", v_int(mem));
    }
    int64_t cu = sysctl_i64("machdep.gpu.core_count");
    if (cu < 0)
        cu = 0;
    mput(gpu, "cores", v_int(cu));
    int64_t ghz = sysctl_i64("machdep.gpu.core_clock_mhz");
    if (ghz > 0)
        mput(gpu, "clock", v_float((double)ghz));
    v_list_append(gpus, gpu);
    v_free(gpu);
    mput(root, "gpu", gpus);
}

static void darwin_disks(V *root) {
    V *disks = v_list(0);
    struct statfs *mnt = NULL;
    int n = getmntinfo(&mnt, MNT_NOWAIT);
    double rd = 0, wr = 0;
    int benched = bench_rw(use_bench_dir, &rd, &wr);
    for (int i = 0; i < n; i++) {
        if (strncmp(mnt[i].f_mntfromname, "devfs", 5) == 0)
            continue;
        V *disk = mdict();
        mput(disk, "device", v_str(mnt[i].f_mntfromname));
        mput(disk, "mount", v_str(mnt[i].f_mntonname));
        mput(disk, "fstype", v_str(mnt[i].f_fstypename));
        mput(disk, "size", v_int( (int64_t)mnt[i].f_blocks * mnt[i].f_bsize) );
        mput(disk, "type", v_str("apfs"));//?
        if (benched == 0 && !strcmp(mnt[i].f_mntonname, "/")) {
            mput(disk, "read", v_float(rd));
            mput(disk, "write", v_float(wr));
            mput(disk, "bench_path", v_str(use_bench_dir));
        }
        v_list_append(disks, disk);
        v_free(disk);
    }
    mput(root, "disks", disks);
}

#define linux_disks darwin_disks

#else

static void fill_cpu(V *root) {
    V *cpu = mdict();
    mput(cpu, "threads", v_int( sysconf(_SC_NPROCESSORS_ONLN)) );
    mput(root, "cpu", cpu);
}

static void fill_ram(V *root) {
    mput(root, "ram", mdict());
}

static void fill_gpus(V *root) {
    mput(root, "gpu", v_list(0));
}

static void linux_disks(V *root) {
    mput(root, "disks", v_list(0));
}

#endif

V *bi_machine(V **a, in) {
    (void)a;
    if (n != 0)
        return v_err("machine()");
    return shakti_machine_info();
}

V *shakti_machine_info(void) {
    const char *e=getenv("TMPDIR");if(e)use_bench_dir=e;
    V *root = mdict();
    fill_id(root);
    fill_os(root);
    fill_cpu(root);
    fill_ram(root);
    fill_gpus(root);
    linux_disks(root);
    return root;
}
