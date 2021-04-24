/*
 * camera-ctl / v4l2 camera control
 * 
 * Petr Vavrin (peterbay)   pvavrin@gmail.com
 *                          https://github.com/peterbay
 *  
 * AlexOD42                 https://github.com/AlexOD42
 *                          firt idea and application skeleton 
 * 
 * Based on this issue      https://github.com/showmewebcam/showmewebcam/issues/56
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <ncurses.h>

#define DEBUG false

#define pixfmtstr(x) (x) & 0xff, ((x) >> 8) & 0xff, ((x) >> 16) & 0xff, ((x) >> 24) & 0xff

#define clamp(val, min, max)                   \
    ({                                         \
        typeof(val) __val = (val);             \
        typeof(min) __min = (min);             \
        typeof(max) __max = (max);             \
        (void)(&__val == &__min);              \
        (void)(&__val == &__max);              \
        __val = __val < __min ? __min : __val; \
        __val > __max ? __max : __val;         \
    })

struct control_option
{
    int index;
    int value;
    char *name;
} control_option;

enum control_entry_type
{
    V4L2_CONTROL,
    V4L2_PARAM,
};

struct control_mapping
{
    int entry_type;
    unsigned int id;
    char *name;
    char *var_name;
    unsigned int control_type;
    int value;
    int minimum;
    int maximum;
    int step;
    int default_value;
    bool hasoptions;
    struct control_option *options;
} control_mapping;

const char *ignored_variables[50];
int last_ignored_variable = 0;

static bool list_controls = false;
static bool disable_unsupported_controls = false;

volatile sig_atomic_t terminate = 0;
static struct control_mapping *ctrl_mapping;
static char *config_file = "/boot/camera.txt";
static char *v4l2_devname = "/dev/video0";
static unsigned int v4l2_dev_pixelformat;
static unsigned int v4l2_dev_width;
static unsigned int v4l2_dev_height;
static int last_offset = 0;
static int ctrl_last = 0;
static int v4l2_dev_fd;
static bool ui_initialized = false;
static int active_control = 0;
static int fps_max = 30;

struct preset
{
    char *path;
    char *name;
};
static char *presets_path = NULL;
static struct preset presets[10] = {
    {NULL, NULL},
    {NULL, NULL},
    {NULL, NULL},
    {NULL, NULL},
    {NULL, NULL},
    {NULL, NULL},
    {NULL, NULL},
    {NULL, NULL},
    {NULL, NULL},
    {NULL, NULL},
};
static int last_preset_loaded = -1;
static bool preset_alpabetically = false;
static int alpha_index = 0;

struct window_dimensions
{
    int top;
    int left;
    int cols;
    int rows;
};

struct window_dimensions top_dim = {0, 0, 0, 0};
struct window_dimensions menu_dim = {0, 0, 0, 0};
struct window_dimensions control_dim = {0, 0, 0, 0};
struct window_dimensions help_dim = {0, 0, 0, 0};

WINDOW *top_win;
WINDOW *menu_win;
WINDOW *control_win;
WINDOW *help_win;

void term(int signum)
{
    (void)(signum); /* avoid warning: unused parameter 'signum' */
    terminate = true;
}

static int v4l2_open(char *devname)
{
    struct v4l2_capability cap;

    v4l2_dev_fd = open(devname, O_RDWR | O_NONBLOCK, 0);
    if (v4l2_dev_fd == -1)
    {
        printf("ERROR: Device open failed: %s (%d)\n", strerror(errno), errno);
        return -EINVAL;
    }

    if (ioctl(v4l2_dev_fd, VIDIOC_QUERYCAP, &cap) < 0)
    {
        printf("ERROR: VIDIOC_QUERYCAP failed: %s (%d)\n", strerror(errno), errno);
        goto err;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
    {
        printf("ERROR: %s is no video capture device\n", devname);
        goto err;
    }
    return 1;

err:
    close(v4l2_dev_fd);
    return -EINVAL;
}

static void v4l2_close()
{
    if (v4l2_dev_fd)
    {
        close(v4l2_dev_fd);
    }
}

static int v4l2_fps_get()
{
    static struct v4l2_streamparm parm;
    struct v4l2_fract *tf;
    memset(&parm, 0, sizeof(parm));

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(v4l2_dev_fd, VIDIOC_G_PARM, &parm) == 0)
    {
        tf = &parm.parm.capture.timeperframe;

        if (!tf->denominator || !tf->numerator)
        {
            return 0;
        }
        else
        {
            return (int)(1.0 * tf->denominator / tf->numerator);
        }
    }
    return 0;
}

static int v4l2_fps_set(int fps)
{
    static struct v4l2_streamparm parm;
    struct v4l2_fract *tf;
    memset(&parm, 0, sizeof(parm));

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1000;
    parm.parm.capture.timeperframe.denominator =
        (uint32_t)(fps * parm.parm.capture.timeperframe.numerator);

    if (ioctl(v4l2_dev_fd, VIDIOC_S_PARM, &parm) == 0)
    {
        tf = &parm.parm.capture.timeperframe;

        if (!tf->denominator || !tf->numerator)
        {
            return 0;
        }
        else
        {
            return (int)(1.0 * tf->denominator / tf->numerator);
        }
    }
    return 0;
}

static void v4l2_init_fps()
{
    int fps = v4l2_fps_get();

    ctrl_mapping[ctrl_last].entry_type = V4L2_PARAM;
    ctrl_mapping[ctrl_last].id = 0;
    ctrl_mapping[ctrl_last].name = "FPS";
    ctrl_mapping[ctrl_last].var_name = "fps";
    ctrl_mapping[ctrl_last].control_type = 0;
    ctrl_mapping[ctrl_last].value = fps;
    ctrl_mapping[ctrl_last].minimum = 1;
    ctrl_mapping[ctrl_last].maximum = fps_max;
    ctrl_mapping[ctrl_last].step = 1;
    ctrl_mapping[ctrl_last].default_value = 30;
    ctrl_last++;

}

static int v4l2_set_ctrl_value(int id, int value)
{
    struct v4l2_control control;

    memset(&control, 0, sizeof(control));
    control.id = id;
    control.value = value;

    return ioctl(v4l2_dev_fd, VIDIOC_S_CTRL, &control);
}

static void v4l2_apply_control(struct control_mapping *mapping)
{
    switch (mapping->entry_type)
    {
    case V4L2_CONTROL:
        v4l2_set_ctrl_value(mapping->id, mapping->value);
        break;

    case V4L2_PARAM:
        if (!strncmp(mapping->var_name, "fps", 3))
        {
            mapping->value = v4l2_fps_set(mapping->value);
        }
        break;

    default:
        break;
    }
}

static char *name2var(char *name)
{
    int i;
    int len_name = strlen(name) - 1;
    char lowercase;
    char out_name[127] = {'\0'};
    bool add_underscore = false;

    for (i = 0; i <= len_name; i++)
    {
        if (isalnum(name[i]))
        {
            if (add_underscore)
            {
                strcat(out_name, "_");
                add_underscore = false;
            }
            lowercase = tolower(name[i]);
            strncat(out_name, &lowercase, 1);
        }
        else
        {
            add_underscore = true;
        }
    }
    return strdup((const char *)out_name);
}

static bool v4l2_check_supported_control(int control_id)
{
    if (v4l2_dev_pixelformat != V4L2_PIX_FMT_H264 &&
        v4l2_dev_pixelformat != V4L2_PIX_FMT_H264_NO_SC &&
        v4l2_dev_pixelformat != V4L2_PIX_FMT_H264_MVC)
    {
        switch (control_id)
        {
        case V4L2_CID_MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY:
        case V4L2_CID_MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY_ENABLE:
        case V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_ACTIVITY:
        case V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_DARK:
        case V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_SMOOTH:
        case V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_STATIC:
        case V4L2_CID_MPEG_MFC51_VIDEO_H264_NUM_REF_PIC_FOR_P:
        case V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM:
        case V4L2_CID_MPEG_VIDEO_H264_ASO:
        case V4L2_CID_MPEG_VIDEO_H264_ASO_SLICE_ORDER:
        case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP:
        case V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE:
        case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
        case V4L2_CID_MPEG_VIDEO_H264_FMO:
        case V4L2_CID_MPEG_VIDEO_H264_FMO_CHANGE_DIRECTION:
        case V4L2_CID_MPEG_VIDEO_H264_FMO_CHANGE_RATE:
        case V4L2_CID_MPEG_VIDEO_H264_FMO_MAP_TYPE:
        case V4L2_CID_MPEG_VIDEO_H264_FMO_RUN_LENGTH:
        case V4L2_CID_MPEG_VIDEO_H264_FMO_SLICE_GROUP:
        case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING:
        case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER:
        case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER_QP:
        case V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_TYPE:
        case V4L2_CID_MPEG_VIDEO_H264_I_PERIOD:
        case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
        case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA:
        case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA:
        case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:
        case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
        case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
        case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
        case V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE:
        case V4L2_CID_MPEG_VIDEO_H264_SEI_FP_CURRENT_FRAME_0:
        case V4L2_CID_MPEG_VIDEO_H264_SEI_FRAME_PACKING:
        case V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT:
        case V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH:
        case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE:
        case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC:
            return false;
        default:
            break;
        }
    }

    if (v4l2_dev_pixelformat != V4L2_PIX_FMT_MPEG4)
    {
        switch (control_id)
        {
        case V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL:
        case V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE:
        case V4L2_CID_MPEG_VIDEO_MPEG4_QPEL:
        case V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP:
        case V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP:
        case V4L2_CID_MPEG_VIDEO_MPEG4_B_FRAME_QP:
            return false;
        default:
            break;
        }
    }
    return true;
}

static void v4l2_get_controls()
{
    const unsigned next_fl = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    struct v4l2_queryctrl queryctrl;
    struct v4l2_control control;
    struct v4l2_querymenu querymenu;
    unsigned int options_count;
    unsigned int option_nr;
    unsigned int id;
    int menu_index;
    char *var_name;
    bool ignore;
    int liv;

    memset(&queryctrl, 0, sizeof(queryctrl));
    memset(&querymenu, 0, sizeof(querymenu));

    ctrl_mapping = malloc(100 * sizeof(struct control_mapping));

    if (list_controls)
    {
        printf("INFO: %30s = %-30s\n", "Control variable name", "Control name");
    }

    queryctrl.id = next_fl;
    while (0 == ioctl(v4l2_dev_fd, VIDIOC_QUERYCTRL, &queryctrl))
    {
        id = queryctrl.id;
        queryctrl.id |= next_fl;

        if ((queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) ||
            (queryctrl.flags & V4L2_CTRL_FLAG_INACTIVE) ||
            (queryctrl.flags & V4L2_CTRL_FLAG_READ_ONLY))
        {
            continue;
        }

        if (disable_unsupported_controls)
        {
            if (!v4l2_check_supported_control(id))
            {
                printf("INFO: Ignore unsupported control: %s\n", queryctrl.name);
                continue;
            }
        }

        control.id = queryctrl.id;
        if (0 == ioctl(v4l2_dev_fd, VIDIOC_G_CTRL, &control))
        {
            option_nr = 0;
            var_name = name2var((char *)queryctrl.name);

            if (list_controls)
            {
                printf("INFO: %30s = %-30s\n", var_name, queryctrl.name);
                continue;
            }

            if (last_ignored_variable > 0)
            {
                ignore = false;
                for (liv = 0; liv < last_ignored_variable; liv++)
                {
                    if (!strncmp(var_name, ignored_variables[liv], strlen(var_name)))
                    {
                        ignore = true;
                        continue;
                    }
                }
                if (ignore)
                {
                    continue;
                }
            }

            ctrl_mapping[ctrl_last].entry_type = V4L2_CONTROL;
            ctrl_mapping[ctrl_last].id = id;
            ctrl_mapping[ctrl_last].name = strdup((const char *)queryctrl.name);
            ctrl_mapping[ctrl_last].var_name = var_name;
            ctrl_mapping[ctrl_last].control_type = queryctrl.type;
            ctrl_mapping[ctrl_last].value = control.value;
            ctrl_mapping[ctrl_last].minimum = queryctrl.minimum;
            ctrl_mapping[ctrl_last].maximum = queryctrl.maximum;
            ctrl_mapping[ctrl_last].step = queryctrl.step;
            ctrl_mapping[ctrl_last].default_value = queryctrl.default_value;

            if (queryctrl.type == V4L2_CTRL_TYPE_MENU || queryctrl.type == V4L2_CTRL_TYPE_INTEGER_MENU)
            {
                options_count = queryctrl.maximum - queryctrl.minimum + 1;
                if (options_count <= 0)
                {
                    continue;
                }
                ctrl_mapping[ctrl_last].options = malloc(options_count * sizeof(struct control_option));

                for (menu_index = queryctrl.minimum; menu_index <= queryctrl.maximum; menu_index++)
                {
                    querymenu.id = id;
                    querymenu.index = menu_index;
                    if (0 == ioctl(v4l2_dev_fd, VIDIOC_QUERYMENU, &querymenu))
                    {
                        ctrl_mapping[ctrl_last].options[option_nr].index = querymenu.index;

                        if (queryctrl.type == V4L2_CTRL_TYPE_MENU)
                        {
                            ctrl_mapping[ctrl_last].options[option_nr].name = strdup((const char *)querymenu.name);
                        }
                        else
                        {
                            ctrl_mapping[ctrl_last].options[option_nr].value = querymenu.value;
                        }
                        option_nr += 1;
                    }
                }
                if (option_nr)
                {
                    ctrl_mapping[ctrl_last].hasoptions = true;
                }
            }

            ctrl_last += 1;
        }
    }
}

static void control_free()
{
    int i;

    for (i = 0; i < ctrl_last; i++)
    {
        if (ctrl_mapping[i].entry_type == V4L2_CONTROL)
        {
            if (ctrl_mapping[i].name)
            {
                free(ctrl_mapping[i].name);
                ctrl_mapping[i].name = NULL;
            }
            if (ctrl_mapping[i].var_name)
            {
                free(ctrl_mapping[i].var_name);
                ctrl_mapping[i].var_name = NULL;
            }
            if (ctrl_mapping[i].hasoptions && ctrl_mapping[i].options)
            {
                free(ctrl_mapping[i].options);
                ctrl_mapping[i].options = NULL;
            }
        }
    }
}

static void control_load(const char *title, const char *filename)
{
    char name[30];
    int value;
    int i;
    FILE *fp = fopen(filename, "r");

    mvprintw(0, 20, "%*s", 60, " ");

    if (fp != NULL)
    {
        // Assume control=value file format
        while (fscanf(fp, "%[^=]=%d\r\n", name, &value) == 2)
        {
            for (i = 0; i < ctrl_last; i++)
            {
                if (strcmp(name, ctrl_mapping[i].var_name) == 0)
                {
                    if (ctrl_mapping[i].value != value)
                    {
                        ctrl_mapping[i].value = value;
                        v4l2_apply_control(&ctrl_mapping[i]);
                    };
                    break;
                }
            }
        }
        mvprintw(0, 20, "%s file %s loaded", title, filename);
        fclose(fp);
    }
    else
    {
        mvprintw(0, 20, "Cannot load %s", filename);
    }
    refresh();
}

static void control_save(const char *title, const char *filename)
{
    int value;
    // Overwrite existing file
    FILE *fp = fopen(filename, "w");

    mvprintw(0, 20, "%*s", 60, " ");
    if (fp != NULL)
    {
        for (int i = 0; i < ctrl_last; i++)
        {
            value = 0;
            if (ctrl_mapping[i].value != ctrl_mapping[i].default_value)
            {
                value = ctrl_mapping[i].value;
                fprintf(fp, "%s=%d\r\n", ctrl_mapping[i].var_name, value);
            }
        }
        fclose(fp);
        mvprintw(0, 20, "%s file %s saved", title, filename);
    }
    else
    {
        mvprintw(0, 20, "Cannot save %s", filename);
    }
    refresh();
}

static int presets_read(const char *fpath,
                        const struct stat *sb,
                        int tflag)
{
    char *file = NULL;
    int index = 0;

    (void)(tflag); /* avoid warning: unused parameter 'tflag' */

    if (!S_ISDIR(sb->st_mode))
    {
        file = (char *)(fpath + strlen(presets_path));

        if (file[0] == '/')
        {
            file++;
        }

        if (preset_alpabetically)
        {
            if (alpha_index < 9)
            {
                presets[alpha_index].path = strdup((const char *)fpath);
                presets[alpha_index].name = strdup((const char *)file);
                printf("%d %s\n", alpha_index, presets[alpha_index].name);
                alpha_index++;
            }
        }
        else if (file[0] > 48 && file[0] < 58)
        {
            index = (int)file[0] - 49;
            presets[index].path = strdup((const char *)fpath);
            file++;

            while (!isalnum((int)file[0]))
            {
                file++;
            }
            presets[index].name = strdup((const char *)file);
        }
    }
    return 0;
}

static int sort_presets(const void *v1, const void *v2)
{
    const struct preset *p1 = (struct preset *)v1;
    const struct preset *p2 = (struct preset *)v2;
    int rc = strcmp(p1->name, p2->name);
    if (rc < 0)
    {
        return -1;
    }
    else if (rc > 0)
    {
        return +1;
    }
    else
    {
        return 0;
    }
}

static void get_preset_files()
{
    if (presets_path)
    {
        ftw(presets_path, presets_read, 20);
        if (preset_alpabetically && alpha_index > 0)
        {
            qsort(presets, alpha_index, sizeof(struct preset), sort_presets);
        }
    }
}

static void load_preset(int index)
{
    if (index >= 0 && index < 9)
    {
        if (presets[index].path)
        {
            control_load("Preset", presets[index].path);
            last_preset_loaded = index;
        }
    }
}

static void load_next_preset()
{
    int i;

    if (last_preset_loaded < 0)
    {
        for (i = 0; i < 9; i++)
        {
            if (presets[i].path)
            {
                load_preset(i);
                return;
            }
        }
    }
    else
    {
        for (i = last_preset_loaded + 1; i < 9; i++)
        {
            if (presets[i].path)
            {
                load_preset(i);
                return;
            }
        }
        for (i = 0; i < last_preset_loaded; i++)
        {
            if (presets[i].path)
            {
                load_preset(i);
                return;
            }
        }
    }
}

static void menu_item(int cid, int y, int x)
{
    struct control_mapping *cm = &ctrl_mapping[cid];
    int idx;
    char *value_diff = " ";
    int row_width = menu_dim.cols - 4;

    if (cm->value > cm->default_value)
    {
        value_diff = "+";
    }
    else if (cm->value < cm->default_value)
    {
        value_diff = "-";
    }

    /* value */
    mvwprintw(menu_win, y, x, "%*d", row_width, cm->value);

    /* option name */
    if (cm->hasoptions)
    {
        for (idx = cm->minimum; idx <= cm->maximum; idx++)
        {
            if (cm->options[idx].index == cm->value)
            {
                if (cm->options[idx].name)
                {
                    mvwprintw(menu_win, y, x, "%*s", row_width, cm->options[idx].name);
                }
                else
                {
                    mvwprintw(menu_win, y, x, "%*d", row_width, cm->options[idx].value);
                }
            }
        }
    }

    /* name */
    mvwprintw(menu_win, y, x, "%s %s", value_diff, cm->name);
}

static void v4l2_format_info()
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(v4l2_dev_fd, VIDIOC_G_FMT, &fmt) < 0)
    {
        return;
    }
    v4l2_dev_pixelformat = fmt.fmt.pix.pixelformat;
    v4l2_dev_width = fmt.fmt.pix.width;
    v4l2_dev_height = fmt.fmt.pix.height;
}

static void ui_uninit()
{
    curs_set(1);
    clrtoeol();
    refresh();
    endwin();
}

static void draw_top()
{
    int i;
    int row = 2;
    int col = 37;

    wclear(top_win);
    mvwin(top_win, top_dim.top, top_dim.left);
    wresize(top_win, top_dim.rows, top_dim.cols);

    mvprintw(0, 1, "Camera control: ", v4l2_devname);
    mvprintw(1, 1, "V4L2:       %s", v4l2_devname);
    mvprintw(2, 1, "Format:     %c%c%c%c", pixfmtstr(v4l2_dev_pixelformat));
    mvprintw(3, 1, "Resolution: %dx%d", v4l2_dev_width, v4l2_dev_height);

    mvprintw(1, 28, "Config: %s", config_file);
    mvprintw(2, 28, "Presets:");

    for (i = 0; i < 10; i++)
    {
        if (presets[i].name)
        {
            if (col + (int)strlen(presets[i].name) + 5 > (int)top_dim.cols)
            {
                row++;
                col = 37;
            }
            if (row < 4)
            {
                mvprintw(row, col, "[%d] %s", i + 1, presets[i].name);
                col += (int)strlen(presets[i].name) + 5;
            }
        }
    }

    wnoutrefresh(top_win);
}

static void draw_menu(bool full_redraw)
{
    int i;
    int x = 2;
    int y = 1;
    int max;
    int offset = 0;
    int btitle_offset = 0;
    int window_lines = menu_dim.rows - 2;

    if (full_redraw)
    {
        wclear(menu_win);
        mvwin(menu_win, menu_dim.top, menu_dim.left);
        wresize(menu_win, menu_dim.rows, menu_dim.cols);
    }
    else
    {
        werase(menu_win);
    }
    box(menu_win, 0, 0);

    if (active_control > window_lines - 1)
    {
        if (active_control >= last_offset && active_control < last_offset + window_lines)
        {
            offset = last_offset;
        }
        else
        {
            offset = active_control - window_lines + 1;
        }
    }
    else if (last_offset > 0)
    {
        if (active_control > last_offset)
        {
            offset = last_offset;
        }
        else
        {
            offset = active_control;
        }
    }
    last_offset = offset;

    max = (ctrl_last >= offset + window_lines) ? offset + window_lines : ctrl_last;

    box(menu_win, 0, 0);
    for (i = offset; i < max; i++)
    {
        if (active_control == i)
        {
            wattron(menu_win, A_REVERSE);
            menu_item(i, y, x);
            wattroff(menu_win, A_REVERSE);
        }
        else
        {
            menu_item(i, y, x);
        }
        y++;
    }

    btitle_offset = menu_dim.cols - 9;
    btitle_offset -= (active_control + 1 < 10) ? 1 : ((active_control + 1 < 100) ? 2 : 3);
    btitle_offset -= (ctrl_last < 10) ? 1 : ((ctrl_last < 100) ? 2 : 3);

    mvwhline(menu_win, 0, 1, ACS_HLINE, menu_dim.cols - 2);
    wmove(menu_win, menu_dim.rows - 1, btitle_offset);
    wprintw(menu_win, "[ %d / %d ]", active_control + 1, ctrl_last);

    wnoutrefresh(menu_win);

    if (full_redraw)
    {
        wnoutrefresh(menu_win);
    }
    else
    {
        wrefresh(menu_win);
    }
}

static void draw_control(bool full_redraw)
{
    struct control_mapping *cm = &ctrl_mapping[active_control];
    int row = 1;
    int idx;

    if (full_redraw)
    {
        wclear(control_win);
        mvwin(control_win, control_dim.top, control_dim.left);
        wresize(control_win, control_dim.rows, control_dim.cols);
    }
    else
    {
        werase(control_win);
    }
    box(control_win, 0, 0);

    mvwprintw(control_win, row++, 2, "%.22s", cm->name);
    mvwprintw(control_win, row++, 2, "Val: %17d", cm->value);
    mvwprintw(control_win, row++, 2, "Min: %17d", cm->minimum);
    mvwprintw(control_win, row++, 2, "Max: %17d", cm->maximum);
    mvwprintw(control_win, row++, 2, "Stp: %17d", cm->step);
    mvwprintw(control_win, row++, 2, "Def: %17d", cm->default_value);
    mvwprintw(control_win, row, 2, "Opt: %*s", 17, "");

    if (cm->hasoptions)
    {
        for (idx = cm->minimum; idx <= cm->maximum; idx++)
        {
            if (cm->options[idx].index == cm->value)
            {
                if (cm->options[idx].name)
                {
                    mvwprintw(control_win, row, 2, "Opt: %17s", cm->options[idx].name);
                }
                else
                {
                    mvwprintw(control_win, row, 2, "Opt: %17d", cm->options[idx].value);
                }
            }
        }
    }

    if (full_redraw)
    {
        wnoutrefresh(control_win);
    }
    else
    {
        wrefresh(control_win);
    }
}

static void draw_help()
{
    int row = help_dim.top;
    int col = help_dim.left;
    int i;

    for (i = row; i < row + 11; i++)
    {
        mvprintw(i, col - 1, " ");
    }

    wclear(help_win);
    werase(help_win);
    mvwin(help_win, help_dim.top, help_dim.left);
    wresize(help_win, help_dim.rows, help_dim.cols);

    mvprintw(row++, col, "Up/Down/Home/End  Navigate");
    mvprintw(row++, col, "Left/Right          Adjust");
    mvprintw(row++, col, "PgDn/PgUp      Jump Adjust");
    mvprintw(row++, col, "1-9       Load preset file");
    mvprintw(row++, col, "Tab     Switch preset file");
    mvprintw(row++, col, "                          ");
    mvprintw(row++, col, "R Reset All  | U Update   ");
    mvprintw(row++, col, "D Default");
    mvprintw(row++, col, "N Minimum    | M Maximum  ");
    mvprintw(row++, col, "L Load       | S Save     ");
    mvprintw(row++, col, "Q Quit");

    wnoutrefresh(help_win);
}

static void draw_ui(int row, int col)
{
    (void)(row);
    int control_width = 26;

    top_dim.top = 0;
    top_dim.left = 0;
    top_dim.cols = col;
    top_dim.rows = 4;

    menu_dim.top = top_dim.rows;
    menu_dim.left = 0;
    menu_dim.cols = col - control_width - 1;
    menu_dim.rows = row - top_dim.rows;

    control_dim.top = top_dim.rows,
    control_dim.left = menu_dim.cols + 1,
    control_dim.cols = control_width,
    control_dim.rows = 9;

    help_dim.top = control_dim.top + control_dim.rows;
    help_dim.left = menu_dim.cols + 1;
    help_dim.cols = control_dim.cols;
    help_dim.rows = menu_dim.rows - control_dim.rows;

    if (!ui_initialized)
    {
        curs_set(0);
        initscr();
        clear();
        noecho();
        cbreak();

        top_win = newwin(top_dim.rows, top_dim.cols, top_dim.top, top_dim.left);
        menu_win = newwin(menu_dim.rows, menu_dim.cols, menu_dim.top, menu_dim.left);
        control_win = newwin(control_dim.rows, control_dim.cols, control_dim.top, control_dim.left);
        help_win = newwin(help_dim.rows, help_dim.cols, help_dim.top, help_dim.left);

        ui_initialized = true;
    }
    resizeterm(row, col);

    touchwin(stdscr);
    wnoutrefresh(stdscr);

    draw_top();
    draw_menu(true);
    draw_control(true);
    draw_help();

    doupdate();
}

static void update_controls()
{
    struct v4l2_control control;
    int i;

    memset(&control, 0, sizeof(struct v4l2_control));

    for (i = 0; i < ctrl_last; i++)
    {
        control.id = ctrl_mapping[i].id;
        if (ioctl(v4l2_dev_fd, VIDIOC_G_CTRL, &control) == 0)
        {
            ctrl_mapping[i].value = control.value;
        }
    }
}

static void win_watch(int sigNo)
{
    struct winsize termSize;
    if (sigNo == SIGWINCH)
    {
        if (isatty(STDIN_FILENO) &&
            ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&termSize) >= 0)
        {
            draw_ui((int)termSize.ws_row, (int)termSize.ws_col);
        }
    }
}

static void init_win_watch()
{
    struct winsize termSize;
    struct sigaction newSigAction;
    struct sigaction oldSigAction;

    if (isatty(STDIN_FILENO) &&
        ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&termSize) >= 0)
    {
        memset(&newSigAction.sa_mask, 0, sizeof(newSigAction.sa_mask));
        newSigAction.sa_flags = 0;
        newSigAction.sa_handler = win_watch;
        sigaction(SIGWINCH, &newSigAction, &oldSigAction);
    }
}

static int init()
{
    struct control_mapping *cm;
    struct winsize termSize;
    int prev_active_control;
    int prev_value;
    bool redraw;
    bool quit = false;
    int c;
    int i;

    if (v4l2_open(v4l2_devname) < 0)
    {
        return 1;
    }

    v4l2_format_info();

    v4l2_get_controls();
    v4l2_init_fps();

    if (list_controls)
    {
        goto end;
    }

    get_preset_files();

    if (isatty(STDIN_FILENO) &&
        ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&termSize) >= 0)
    {
        draw_ui((int)termSize.ws_row, (int)termSize.ws_col);
    }
    else
    {
        draw_ui(24, 80);
    }

    init_win_watch();

    keypad(stdscr, TRUE);

    while (!quit)
    {
        c = getch();

        cm = &ctrl_mapping[active_control];
        prev_value = cm->value;
        prev_active_control = active_control;
        redraw = false;

        switch (c)
        {
        case KEY_UP:
            active_control -= 1;
            break;

        case KEY_DOWN:
            active_control += 1;
            break;

        case KEY_LEFT:
            cm->value -= cm->step;
            break;

        case KEY_RIGHT:
            cm->value += cm->step;
            break;

        case 338: // Page Down
            cm->value -= cm->step * 10;
            break;

        case 339: // Page Up
            cm->value += cm->step * 10;
            break;

        case 262: // Home
            active_control = 0;
            break;

        case 360: // END
            active_control = ctrl_last - 1;
            break;

        case '1':
            load_preset(0);
            redraw = true;
            break;

        case '2':
            load_preset(1);
            redraw = true;
            break;

        case '3':
            load_preset(2);
            redraw = true;
            break;

        case '4':
            load_preset(3);
            redraw = true;
            break;

        case '5':
            load_preset(4);
            redraw = true;
            break;

        case '6':
            load_preset(5);
            redraw = true;
            break;

        case '7':
            load_preset(6);
            redraw = true;
            break;

        case '8':
            load_preset(7);
            redraw = true;
            break;

        case '9':
            load_preset(8);
            redraw = true;
            break;

        case 9:
            load_next_preset();
            redraw = true;
            break;

        case 'N':
        case 'n':
            cm->value = cm->minimum;
            break;

        case 'M':
        case 'm':
            cm->value = cm->maximum;
            break;

        case 'D':
        case 'd':
            cm->value = cm->default_value;
            break;

        case 'R':
        case 'r':
            for (i = 0; i < ctrl_last; i++)
            {
                ctrl_mapping[i].value = ctrl_mapping[i].default_value;
                v4l2_apply_control(&ctrl_mapping[i]);
            }
            redraw = true;
            break;

        case 'L':
        case 'l':
            mvprintw(0, 20, "%*s", 58, " ");
            if (!DEBUG)
            {
                control_load("Config", config_file);
                redraw = true;
            }
            break;

        case 'S':
        case 's':
            mvprintw(0, 20, "%*s", 58, " ");
            if (!DEBUG)
            {
                control_save("Config", config_file);
            }
            break;

        case 'Q':
        case 'q':
            quit = true;
            break;

        case 'U':
        case 'u':
            update_controls();
            redraw = true;
            break;

        default:
            if (DEBUG)
            {
                mvprintw(0, 0, "Character %3d '%c'", c, c);
                refresh();
            }
            break;
        }

        cm->value = clamp(cm->value, cm->minimum, cm->maximum);
        active_control = clamp(active_control, 0, ctrl_last - 1);

        if (prev_value != cm->value)
        {
            v4l2_apply_control(cm);
            redraw = true;
        }

        if (prev_active_control != active_control)
        {
            redraw = true;
        }

        if (redraw)
        {
            draw_control(false);
            draw_menu(false);
        }
    }

    ui_uninit();

end:
    v4l2_close();
    control_free();
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s [options]\n", argv0);
    fprintf(stderr, "Available options are\n");
    fprintf(stderr, " -a                    Load preset files in alphabetical order\n");
    fprintf(stderr, " -c file               Path to config file\n");
    fprintf(stderr, " -d                    Disable unsupported controls\n");
    fprintf(stderr, " -f fps                Maximum FPS value (b/w 1 and 120, default: 30)\n");
    fprintf(stderr, " -h                    Print this help screen and exit\n");
    fprintf(stderr, " -i control_variable   Ignore control with defined name\n");
    fprintf(stderr, " -l                    List available controls\n");
    fprintf(stderr, " -p path               Path to directory with preset files\n");
    fprintf(stderr, " -v device             V4L2 Video Capture device\n");
}

int main(int argc, char *argv[])
{
    int opt;

    while ((opt = getopt(argc, argv, "ac:df:hi:lp:v:")) != -1)
    {
        switch (opt)
        {
        case 'a':
            preset_alpabetically = true;
            break;

        case 'c':
            config_file = optarg;
            break;

        case 'd':
            disable_unsupported_controls = true;
            break;

        case 'f':
            if (atoi(optarg) > 0 && atoi(optarg) < 121)
            {
                fps_max = atoi(optarg);
            }
            else
            {
                printf("ERROR: Invalid maximum fps '%s'\n", optarg);
                return 1;
            }
            break;

        case 'h':
            usage(argv[0]);
            return 1;

        case 'i':
            if (last_ignored_variable < 49)
            {
                printf("INFO: Ignored control: %s\n", optarg);
                ignored_variables[last_ignored_variable] = optarg;
                last_ignored_variable += 1;
            }
            break;

        case 'l':
            list_controls = true;
            break;

        case 'p':
            presets_path = optarg;
            break;

        case 'v':
            v4l2_devname = optarg;
            break;

        default:
            printf("ERROR: Invalid option '-%c'\n", opt);
            goto err;
        }
    }

    return init();

err:
    usage(argv[0]);
    return 1;
}