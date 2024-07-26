#include <errno.h>

#include <windows.h>
#include <winuser.h>
#include <windows.h>
#include <winuser.h>
#include <wingdi.h>

#include <highlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <icm.h>

#include <libjj/logging.h>
#include <libjj/opts.h>
#include <libjj/iconv.h>

// for MSVC
#if defined(_MSC_VER)
#pragma comment(lib, "mscms.lib")
#endif

// for MSYS2
// gcc reload_icc.c /c/Windows/System32/mscms.dll

#if defined(__MINGW32__) || defined(__MINGW64__)
extern BOOL WINAPI WcsGetCalibrationManagementState(BOOL *pbIsEnabled);
extern int WINAPI InternalRefreshCalibration(int, int);
#endif

#define MONITOR_MAX                     (8)
#define for_each_monitor(i) for (size_t (i) = 0; (i) < ARRAY_SIZE(minfo); (i)++)

struct monitor_info {
        uint8_t active;
        uint8_t is_primary;
        struct {
                int32_t x;
                int32_t y;
                uint32_t w;
                uint32_t h;
                uint32_t orientation;
                uint32_t hz;
        } desktop;
        struct {
                wchar_t name[256];
                wchar_t dev_path[256];
                wchar_t reg_path[256];
        } str;
        struct {
                size_t mcnt;
                PHYSICAL_MONITOR *monitor_list;
                HMONITOR enum_monitor;
                HMONITOR phy_monitor;
                DISPLAYCONFIG_MODE_INFO *mode;
        } handle;
};

struct monitor_info minfo[MONITOR_MAX];

DISPLAYCONFIG_PATH_INFO *disp_path;
DISPLAYCONFIG_MODE_INFO *disp_mode;
uint32_t path_cnt, mode_cnt;

int verbose = 0;
int list_monitor = 0;
int target_monitor = -2;
int hdr_toggle = 0;
int hdr_on = 0;
int hdr_off = 0;
char monitor_name[256];

lopt_strbuf(monitor_name, monitor_name, sizeof(monitor_name), "Monitor name");
lopt_noarg(hdr_on, &hdr_on, sizeof(hdr_on), &(int){ 1 }, "Turn on HDR on a monitor");
lopt_noarg(hdr_off, &hdr_off, sizeof(hdr_off), &(int){ 1 }, "Turn off HDR on a monitor");
lsopt_noarg(t, toggle, &hdr_toggle, sizeof(hdr_toggle), &(int){ 1 }, "Toggle HDR state on a monitor");
lsopt_noarg(v, verbose, &verbose, sizeof(verbose), &(int){ 1 }, "Verbose output");
lsopt_noarg(l, list, &list_monitor, sizeof(list_monitor), &(int){ 1 }, "List monitors");
lsopt_int(m, monitor, &target_monitor, sizeof(target_monitor), "Monitor index");
lsopt_noarg(a, all, &target_monitor, sizeof(target_monitor), &(int){ -1 }, "Apply to all monitors");

int color_calibration_reload(void)
{
        BOOL calibration_en = 0;

        if (WcsGetCalibrationManagementState(&calibration_en) == FALSE) {
                pr_err("WcsGetCalibrationManagementState()\n");
                return -EFAULT;
        }

        if (!calibration_en)
                return 0;

        InternalRefreshCalibration(0, 0);

        return 0;
}

int displayinfo_init(void)
{
        DISPLAYCONFIG_PATH_INFO *path;
        DISPLAYCONFIG_MODE_INFO *mode;
        uint32_t pathcnt, modecnt;
        int err = 0;

        err = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathcnt, &modecnt);
        if (err != ERROR_SUCCESS) {
                pr_err("GetDisplayConfigBufferSizes(): 0x%08x\n", err);
                return -EIO;
        }

        if (pathcnt == 0 && modecnt == 0)
                return -EINVAL;

        path = calloc(pathcnt, sizeof(DISPLAYCONFIG_PATH_INFO));
        mode = calloc(modecnt, sizeof(DISPLAYCONFIG_MODE_INFO));

        if (!path || !mode) {
                if (path)
                        free(path);

                if (mode)
                        free(mode);

                return -ENOMEM;
        }

        err = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,
                                 &pathcnt, path,
                                 &modecnt, mode,
                                 0);
        if (err != ERROR_SUCCESS) {
                pr_err("QueryDisplayConfig(): 0x%08x\n", err);
                err = -EFAULT;
                free(path);
                free(mode);
        }

        disp_path = path;
        disp_mode = mode;
        path_cnt = pathcnt;
        mode_cnt = modecnt;

        return err;
}

void displayinfo_deinit(void)
{
        if (disp_path)
                free(disp_path);

        if (disp_mode)
                free(disp_mode);
}

static DISPLAYCONFIG_MODE_INFO *monitor_get(uint32_t path_idx)
{
        if (path_idx >= path_cnt)
                return NULL;

        for (size_t i = 0; i < mode_cnt; i++) {
                DISPLAYCONFIG_MODE_INFO *mode = &disp_mode[i];

                if (mode->id != disp_path[path_idx].targetInfo.id)
                        continue;

                if (mode->infoType != DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
                        continue;

                return mode;
        }

        return NULL;
}

int color_info_get(DISPLAYCONFIG_MODE_INFO *mode, DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO *info)
{
        int err;

        memset(info, 0, sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO));
        info->header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        info->header.size = sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO);
        info->header.adapterId.HighPart = mode->adapterId.HighPart;
        info->header.adapterId.LowPart = mode->adapterId.LowPart;
        info->header.id = mode->id;

        if (ERROR_SUCCESS != (err = DisplayConfigGetDeviceInfo(&info->header))) {
                pr_err("DisplayConfigGetDeviceInfo(): 0x%08x\n", err);
                return -EIO;
        }

        return 0;
}

int color_info_set(DISPLAYCONFIG_MODE_INFO *mode, DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE *state)
{
        int err;

        state->header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
        state->header.size = sizeof(DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE);
        state->header.adapterId.HighPart = mode->adapterId.HighPart;
        state->header.adapterId.LowPart = mode->adapterId.LowPart;
        state->header.id = mode->id;

        if (ERROR_SUCCESS != (err = DisplayConfigSetDeviceInfo(&state->header))) {
                pr_err("DisplayConfigSetDeviceInfo(): 0x%08x\n", err);
                return -EIO;
        }

        return 0;
}

int hdr_get(DISPLAYCONFIG_MODE_INFO *mode)
{
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO info;
        int ret;

        if (!mode)
                return -ENOENT;

        if ((ret = color_info_get(mode, &info)))
                return ret;

        if (info.advancedColorSupported && info.advancedColorEnabled)
                return 1;

        return 0;
}

int hdr_support_get(DISPLAYCONFIG_MODE_INFO *mode)
{
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO info;
        int ret;

        if (!mode)
                return -ENOENT;

        if ((ret = color_info_get(mode, &info)))
                return ret;

        if (info.advancedColorSupported)
                return 1;

        return 0;
}

int hdr_set(DISPLAYCONFIG_MODE_INFO *mode, uint32_t enabled)
{
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO info = { 0 };
        DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE state = { 0 };
        int err;

        if (!mode)
                return -ENOENT;

        if ((err = color_info_get(mode, &info)))
                return err;

        if (!info.advancedColorSupported)
                return -ENOTSUP;

        if (info.advancedColorEnabled == !!enabled)
                return 0;

        state.enableAdvancedColor = !!enabled;
        if ((err = color_info_set(mode, &state)))
                return err;

        Sleep(2 * 1000);
        color_calibration_reload();
        
        return 0;
}

int colordepth_get(DISPLAYCONFIG_MODE_INFO *mode)
{
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO info;
        int ret;

        if (!mode)
                return -ENOENT;

        if ((ret = color_info_get(mode, &info)))
                return ret;

        return info.bitsPerColorChannel;
}

int sdr_level_get(DISPLAYCONFIG_MODE_INFO *mode)
{
        DISPLAYCONFIG_SDR_WHITE_LEVEL info = { 0 };
        int err;

        if (!mode)
                return -ENOENT;

        info.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        info.header.size = sizeof(DISPLAYCONFIG_SDR_WHITE_LEVEL);
        info.header.adapterId.HighPart = mode->adapterId.HighPart;
        info.header.adapterId.LowPart = mode->adapterId.LowPart;
        info.header.id = mode->id;

        if (ERROR_SUCCESS != (err = DisplayConfigGetDeviceInfo(&info.header))) {
                pr_err("DisplayConfigGetDeviceInfo(): 0x%08x\n", err);
                return -EIO;
        }

        return (int)info.SDRWhiteLevel;
}

void monitor_info_list(void)
{
        pr_raw("Monitor Name           Resolution  HDRSupported HDREnabled ColorDepth SDRWhiteLevel\n");

        for_each_monitor(i) {
                struct monitor_info *m = &minfo[i];

                if (!m->active)
                        continue;

                if (m->handle.mode) {
                        DISPLAYCONFIG_MODE_INFO *mode = m->handle.mode;
                        DISPLAYCONFIG_2DREGION *size = &mode->targetMode.targetVideoSignalInfo.activeSize;
                        int sdr_level = sdr_level_get(mode);

                        pr_raw("%-7ju %-14ls %-5ux%5u %12d %10d %10d %4d (%-3.0fnits)\n",
                               i, m->str.name, size->cx, size->cy, hdr_support_get(mode), hdr_get(mode), colordepth_get(mode), sdr_level, (float)sdr_level / 1000.0 * 80.0);
                }
        }
}

void __monitor_info_free(struct monitor_info *m)
{
        if (!m->active)
                return;

        if (m->handle.monitor_list) {
                DestroyPhysicalMonitors(m->handle.mcnt, m->handle.monitor_list);
                free(m->handle.monitor_list);
        }

        memset(m, 0, sizeof(struct monitor_info));
}

int monitors_info_free(void)
{
        for_each_monitor(i) {
                __monitor_info_free(&minfo[i]);
        }

        return 0;
}

int monitor_target_get(wchar_t *device, DISPLAYCONFIG_TARGET_DEVICE_NAME *target)
{
        DISPLAYCONFIG_PATH_INFO *paths = NULL;
        DISPLAYCONFIG_MODE_INFO *modes = NULL;
        uint32_t n_path, n_mode;
        int err = -ENOENT;

        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,
                                        &n_path,
                                        &n_mode)
            != ERROR_SUCCESS) {
                pr_getlasterr("GetDisplayConfigBufferSizes");
                return -EINVAL;
        }

        paths = calloc(n_path, sizeof(DISPLAYCONFIG_PATH_INFO));
        modes = calloc(n_mode, sizeof(DISPLAYCONFIG_MODE_INFO));

        if (!paths || !modes) {
                err = -ENOMEM;
                goto out_free;
        }

        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,
                               &n_path,
                               paths,
                               &n_mode,
                               modes,
                               NULL) != ERROR_SUCCESS) {
                pr_getlasterr("QueryDisplayConfig");
                err = -EINVAL;

                goto out_free;
        }

        for (size_t i = 0; i < n_path; i++) {
                const DISPLAYCONFIG_PATH_INFO *path = &paths[i];
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source = { };

                source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                source.header.size = sizeof(source);
                source.header.adapterId = path->sourceInfo.adapterId;
                source.header.id = path->sourceInfo.id;

                if ((DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS) &&
                    (0 == wcscmp(device, source.viewGdiDeviceName))) {
                        target->header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
                        target->header.size = sizeof(*target);
                        target->header.adapterId = path->sourceInfo.adapterId;
                        target->header.id = path->targetInfo.id;

                        if (DisplayConfigGetDeviceInfo(&target->header) != ERROR_SUCCESS) {
                                pr_getlasterr("DisplayConfigGetDeviceInfo");
                                err = -EIO;
                                break;
                        }

                        err = 0;
                        break;
                }

        }

out_free:
        if (paths)
                free(paths);

        if (modes)
                free(modes);

        return err;
}

int phy_monitor_desc_get(struct monitor_info *m, HMONITOR monitor)
{
        MONITORINFOEXW info = { };
        DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
        info.cbSize = sizeof(info);
        int err;

        if (GetMonitorInfoW(monitor, (MONITORINFO *)&info) == FALSE) {
                pr_getlasterr("GetMonitorInfo");
                return -1;
        }

        if ((err = monitor_target_get(info.szDevice, &target))) {
                return err;
        }

        __wstr_ncpy(m->str.name, target.monitorFriendlyDeviceName, WCBUF_LEN(m->str.name));
        __wstr_ncpy(m->str.reg_path, target.monitorDevicePath, WCBUF_LEN(m->str.reg_path));
        __wstr_ncpy(m->str.dev_path, info.szDevice, WCBUF_LEN(m->str.dev_path));

        if (info.dwFlags == MONITORINFOF_PRIMARY) {
                pr_rawlvl(VERBOSE, "monitor is primary\n");
                m->is_primary = 1;
        }

        pr_rawlvl(VERBOSE, "monitor name: \"%ls\"\n", target.monitorFriendlyDeviceName);
        pr_rawlvl(VERBOSE, "manufacture id: %x\n", target.edidManufactureId);
        pr_rawlvl(VERBOSE, "product id: %x\n", target.edidProductCodeId);
        pr_rawlvl(VERBOSE, "device path: \"%ls\"\n", info.szDevice);
        pr_rawlvl(VERBOSE, "device reg path: \"%ls\"\n", target.monitorDevicePath);

        return 0;
}

int phy_monitor_enum_cb(HMONITOR monitor, HDC hdc, RECT *rect, LPARAM param)
{
        int *reset_idx = (void *)param;
        static size_t i = 0;

        if (*reset_idx) {
                i = 0;
                *reset_idx = 0;
        }

        struct monitor_info *m = &minfo[i];

        pr_rawlvl(VERBOSE, "monitor %zu:\n", i);

        if (phy_monitor_desc_get(m, monitor))
                return FALSE;

        {
                DWORD cnt = 0;

                GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &cnt);

                if (cnt != 1)
                        pr_warn("associated monitor cnt = %lu\n", cnt);

                LPPHYSICAL_MONITOR monitor_list = calloc(1, cnt * sizeof(PHYSICAL_MONITOR));

                if (GetPhysicalMonitorsFromHMONITOR(monitor, cnt, monitor_list) == FALSE) {
                        pr_getlasterr("GetPhysicalMonitorsFromHMONITOR");
                        free(monitor_list);

                        return FALSE;
                }

                // pr_rawlvl(VERBOSE, "desc: %ls\n", monitor_list[0].szPhysicalMonitorDescription);

                m->handle.mcnt = cnt;
                m->handle.enum_monitor = monitor;
                m->handle.monitor_list = monitor_list;
                m->handle.phy_monitor = monitor_list[0].hPhysicalMonitor;
        }

        m->active = 1;

        i++;

        if (i >= ARRAY_SIZE(minfo)) {
                pr_err("monitor idx is overflow\n");
                return FALSE;
        }

        return TRUE;
}

int __monitor_desktop_info_update(struct monitor_info *m, DISPLAY_DEVICE *dev, DEVMODE *mode)
{
        m->desktop.x = mode->dmPosition.x;
        m->desktop.y = mode->dmPosition.y;
        m->desktop.w = mode->dmPelsWidth;
        m->desktop.h = mode->dmPelsHeight;
        m->desktop.orientation = mode->dmDisplayOrientation;
        m->desktop.hz = mode->dmDisplayFrequency;

        return 0;
}

int monitor_desktop_info_update(DISPLAY_DEVICE *dev, DEVMODE *mode)
{
        for_each_monitor(i) {
                struct monitor_info *m = &minfo[i];

                if (!m->active)
                        continue;

                if (!is_wstr_equal(dev->DeviceName, m->str.dev_path))
                        continue;

                return __monitor_desktop_info_update(m, dev, mode);
        }

        return -ENODEV;
}

int virtual_desktop_info_update(void)
{
        for (DWORD i = 0; ; i++) {
                DISPLAY_DEVICE dev = { .cb = sizeof(DISPLAY_DEVICE) };
                DEVMODE mode = { .dmSize = sizeof(DEVMODE) };

                if (FALSE == EnumDisplayDevices(NULL, i, &dev, 0)) {
                        if (i == 0) {
                                pr_getlasterr("EnumDisplayDevices");
                                return -EIO;
                        }

                        break;
                }

                if (0 == (dev.StateFlags & DISPLAY_DEVICE_ACTIVE)) {
                        pr_rawlvl(VERBOSE, "Display #%lu (not active)\n", i);
                        continue;
                }

                if ((dev.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER)) {
                        pr_rawlvl(VERBOSE, "Display #%lu (mirroring)\n", i);
                        continue;
                }

                pr_rawlvl(VERBOSE, "Display #%lu\n", i);
                pr_rawlvl(VERBOSE, "\tName:   %ls\n", dev.DeviceName);
                pr_rawlvl(VERBOSE, "\tString: %ls\n", dev.DeviceString);
                pr_rawlvl(VERBOSE, "\tFlags:  0x%08lx\n", dev.StateFlags);
                pr_rawlvl(VERBOSE, "\tRegKey: %ls\n", dev.DeviceKey);

                if (!EnumDisplaySettings(dev.DeviceName, ENUM_CURRENT_SETTINGS, &mode)) {
                        pr_getlasterr("EnumDisplaySettings\n");
                        continue;
                }

                //
                // the primary display is always located at 0,0
                //

                pr_rawlvl(VERBOSE, "\tMode: %lux%lu @ %lu Hz %lu bpp\n", mode.dmPelsWidth, mode.dmPelsHeight, mode.dmDisplayFrequency, mode.dmBitsPerPel);
                pr_rawlvl(VERBOSE, "\tOrientation: %lu\n", mode.dmDisplayOrientation);
                pr_rawlvl(VERBOSE, "\tDesktop position: ( %ld, %ld )\n", mode.dmPosition.x, mode.dmPosition.y);

                monitor_desktop_info_update(&dev, &mode);

                pr_rawlvl(VERBOSE, "\tAvailable modes:\n");
                int j = 0;
                while (EnumDisplaySettings(dev.DeviceName, j, &mode)) {
                        pr_rawlvl(VERBOSE, "\t\t%lux%lu %luHz\n", mode.dmPelsWidth, mode.dmPelsHeight, mode.dmDisplayFrequency);
                        j++;
                }
        }

        return 0;
}

int monitor_info_update(void)
{
        int reset_idx = 1;
        int err = 0;

        monitors_info_free();

        if (EnumDisplayMonitors(NULL, NULL, phy_monitor_enum_cb, (intptr_t)&reset_idx) == FALSE) {
                pr_getlasterr("EnumDisplayMonitors\n");
                return -EIO;
        }

        if ((err = virtual_desktop_info_update()))
                return err;

        return err;
}

void monitor_info_link(void)
{
        for_each_monitor(i) {
                struct monitor_info *m = &minfo[i];
                DISPLAYCONFIG_MODE_INFO *mode = monitor_get(i);

                if (!m->active)
                        continue;

                if (!mode) {
                        pr_err("failed to get DISPLAYCONFIG_MODE_INFO for monitor %zu\n", i);
                        continue;
                }

                m->handle.mode = mode;
        }
}

int monitor_index_get_by_name(char *name)
{
        wchar_t _name[256] = { };

        iconv_utf82wc(name, strlen(name), _name, sizeof(_name));

        for_each_monitor(i) {
                struct monitor_info *m = &minfo[i];

                if (!m->active)
                        continue;

                if (is_wstr_equal(_name, m->str.name))
                        return (int)i;
        }

        return -1;
}

int wmain(int wargc, wchar_t *wargv[])
{
        int err;

        setbuf(stdout, NULL);

        logging_colored_set(0);
        console_alloc_set(0);
        opts_helptxt_defval_print(0);

        if (wargc == 1) {
                print_help();
                return -EINVAL;
        }

        if ((err = wchar_longopts_parse(wargc, wargv, NULL)))
                return err;

        if ((err = logging_init()))
                return err;

        console_hide();

        monitor_info_update();
        displayinfo_init();
        monitor_info_link();

        if (is_strptr_set(monitor_name)) {
                target_monitor = monitor_index_get_by_name(monitor_name);
        }

        if (target_monitor >= (int)path_cnt) {
                pr_err("Invalid monitor index\n");
                goto end;
        }

        if (!verbose) {
                g_logprint_level &= ~LOG_LEVEL_VERBOSE;
        }

        if (list_monitor) {
                monitor_info_list();
                goto end;
        }

        if (hdr_toggle) {
                struct monitor_info *m = &minfo[target_monitor];

                if (target_monitor < 0) {
                        pr_rawlvl(ERROR, "Toggle HDR needs to specify a monitor\n");
                        goto end;
                }

                if ((err = hdr_get(m->handle.mode)) < 0) {
                        goto end;
                }

                err = hdr_set(m->handle.mode, !err);

                goto end;
        }

        if (target_monitor == -1 && (hdr_on || hdr_off)) {
                for_each_monitor(i) {
                        struct monitor_info *m = &minfo[i];
                        int enabled;

                        if (hdr_on)
                                enabled = 1;
                        else if (hdr_off)
                                enabled = 0;

                        if (!m->active)
                                continue;

                        if ((err = hdr_set(m->handle.mode, enabled))) {
                                pr_rawlvl(ERROR, "monitor %ls failed to set HDR state\n", m->str.name);
                        }
                }

                goto end;
        }

        if (hdr_on || hdr_off) {
                struct monitor_info *m = &minfo[target_monitor];

                if (target_monitor < 0) {
                        pr_rawlvl(ERROR, "Toggle HDR needs to specify a monitor\n");
                        goto end;
                }

                if (hdr_on) {
                        err = hdr_set(m->handle.mode, 1);
                } else if (hdr_off) {
                        err = hdr_set(m->handle.mode, 0);
                }

                goto end;
        }

end:
        displayinfo_deinit();

        logging_exit();

        return err;
}
