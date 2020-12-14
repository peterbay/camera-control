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
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <ncurses.h>

#define DEBUG false
#define WINDOW_WIDTH 50
#define WINDOW_HEIGHT 20

#define pixfmtstr(x) (x) & 0xff, ((x) >> 8) & 0xff, ((x) >> 16) & 0xff, ((x) >> 24) & 0xff

enum control_actions {
    CONTROL_DECREASE_STEP = 1,
    CONTROL_INCREASE_STEP,
    CONTROL_DESREASE_INCREMENT,
    CONTROL_INCREASE_INCREMENT,
    CONTROL_MINIMUM,
    CONTROL_MAXIMUM,
    CONTROL_DEFAULT,
};

struct control_option {
    int index;
    int value;
    char * name;
} control_option;

struct control_mapping {
    unsigned int id;
    char * name;
    char * var_name;
    unsigned int control_type;
    int value;
    int minimum;
    int maximum;
    int step;
    int default_value;
    bool hasoptions;
    struct control_option * options;
} control_mapping;

const char * ignored_variables[50];
int last_ignored_variable = 0;

static bool list_controls = false;
static bool disable_unsupported_controls = false;

volatile sig_atomic_t terminate = 0;
static struct control_mapping * ctrl_mapping;
static int ctrl_last = 0;
static char * v4l2_devname = "/dev/video0";
static int v4l2_dev_fd;
static unsigned int v4l2_dev_pixelformat;
static char * config_file = "/boot/camera.txt";
static int highlight = 0;
static int last_offset = 0;

void term(int signum)
{
    (void)(signum); /* avoid warning: unused parameter 'signum' */
    terminate = true;
}

static int v4l2_open(char * devname)
{
    struct v4l2_capability cap;

    printf("INFO: Opening %s device\n", devname);

    v4l2_dev_fd = open(devname, O_RDWR | O_NONBLOCK, 0);
    if (v4l2_dev_fd == -1) {
        printf("ERROR: Device open failed: %s (%d)\n", strerror(errno), errno);
        return -EINVAL;
    }

    if (ioctl(v4l2_dev_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        printf("ERROR: VIDIOC_QUERYCAP failed: %s (%d)\n", strerror(errno), errno);
        goto err;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        printf("ERROR: %s is no video capture device\n", devname);
        goto err;
    }

    printf("INFO: Device is %s on bus %s\n", cap.card, cap.bus_info);
    return 1;

err:
    close(v4l2_dev_fd);
    return -EINVAL;
}

static void v4l2_close()
{
    if (v4l2_dev_fd) {
        close(v4l2_dev_fd);
    }
}

static int v4l2_set_ctrl_value(int id, int value)
{
    struct v4l2_control control;

    memset(&control, 0, sizeof(control));
    control.id = id;
    control.value = value;

    return ioctl(v4l2_dev_fd, VIDIOC_S_CTRL, &control);
}

static char * name2var(char * name)
{
    int i;
    int len_name = strlen(name) - 1;
    char lowercase;
    char out_name[127] = { '\0' };
    bool add_underscore = false;

    for(i = 0; i <= len_name; i++) {
        if(isalnum(name[i])) {
            if (add_underscore) {
                strcat(out_name, "_");
                add_underscore = false;
            }
            lowercase = tolower(name[i]);
            strncat(out_name, &lowercase, 1);
        } else {
            add_underscore = true;
        }
    }
    return strdup((const char *) out_name);
}

static bool v4l2_check_supported_control(int control_id)
{
    if (v4l2_dev_pixelformat != V4L2_PIX_FMT_H264 &&
        v4l2_dev_pixelformat != V4L2_PIX_FMT_H264_NO_SC &&
        v4l2_dev_pixelformat != V4L2_PIX_FMT_H264_MVC
    ) {
        switch (control_id) {
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

    if (v4l2_dev_pixelformat != V4L2_PIX_FMT_MPEG4) {
        switch (control_id) {
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
    struct v4l2_queryctrl queryctrl;
    struct v4l2_control control;
    struct v4l2_querymenu querymenu;
    unsigned int id;
    unsigned int options_count;
    unsigned int option_nr;
    int menu_index;
    int liv;
    char * var_name;
    const unsigned next_fl = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    bool ignore;
    memset(&queryctrl, 0, sizeof(queryctrl));
    memset(&querymenu, 0, sizeof (querymenu));

    ctrl_mapping = malloc(100 * sizeof(struct control_mapping));

    if (list_controls) {
        printf("INFO: %30s = %-30s\n", "Control variable name", "Control name");
    }

    queryctrl.id = next_fl;
    while (0 == ioctl (v4l2_dev_fd, VIDIOC_QUERYCTRL, &queryctrl)) {

        id = queryctrl.id;
        queryctrl.id |= next_fl;

        if ((queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) ||
            (queryctrl.flags & V4L2_CTRL_FLAG_INACTIVE) ||
            (queryctrl.flags & V4L2_CTRL_FLAG_READ_ONLY)
        ) {
            continue;
        }

        if (disable_unsupported_controls) {
            if (!v4l2_check_supported_control(id)) {
                printf("INFO: Ignore unsupported control: %s\n", queryctrl.name);
                continue;
            }
        }

        control.id = queryctrl.id;
        if (0 == ioctl (v4l2_dev_fd, VIDIOC_G_CTRL, &control)) {
            option_nr = 0;
            var_name = name2var((char *) queryctrl.name);

            if (list_controls) {
                printf("INFO: %30s = %-30s\n", var_name, queryctrl.name);
                continue;
            }

            if (last_ignored_variable > 0) {
                ignore = false;
                for (liv = 0; liv < last_ignored_variable; liv++) {
                    if (!strncmp(var_name, ignored_variables[liv], strlen(var_name))) {
                        ignore = true;
                        continue;
                    }
                }
                if (ignore) {
                    continue;
                }
            }

            ctrl_mapping[ctrl_last].id            = id;
            ctrl_mapping[ctrl_last].name          = strdup((const char *) queryctrl.name);
            ctrl_mapping[ctrl_last].var_name      = var_name;
            ctrl_mapping[ctrl_last].control_type  = queryctrl.type;
            ctrl_mapping[ctrl_last].value         = control.value;
            ctrl_mapping[ctrl_last].minimum       = queryctrl.minimum;
            ctrl_mapping[ctrl_last].maximum       = queryctrl.maximum;
            ctrl_mapping[ctrl_last].step          = queryctrl.step;
            ctrl_mapping[ctrl_last].default_value = queryctrl.default_value;

            if (queryctrl.type == V4L2_CTRL_TYPE_MENU || queryctrl.type == V4L2_CTRL_TYPE_INTEGER_MENU) {
                options_count = queryctrl.maximum - queryctrl.minimum + 1;
                if (options_count <= 0) {
                    continue;
                }
                ctrl_mapping[ctrl_last].options = malloc(options_count * sizeof(struct control_option));

                for (menu_index = queryctrl.minimum; menu_index <= queryctrl.maximum; menu_index++) {
                    querymenu.id = id;
                    querymenu.index = menu_index;
                    if (0 == ioctl (v4l2_dev_fd, VIDIOC_QUERYMENU, &querymenu)) {
                        ctrl_mapping[ctrl_last].options[option_nr].index = querymenu.index;

                        if (queryctrl.type == V4L2_CTRL_TYPE_MENU) {
                            ctrl_mapping[ctrl_last].options[option_nr].name = strdup((const char *) querymenu.name);
                            
                        } else {
                            ctrl_mapping[ctrl_last].options[option_nr].value = querymenu.value;
                            
                        }
                        option_nr += 1;
                    }
                }
                if (option_nr) {
                    ctrl_mapping[ctrl_last].hasoptions = true;
                }
            }

            ctrl_last += 1;
        }
    }
}

static int control_change(int action, int ci)
{
    int increment = 0;
    int prev_value = ctrl_mapping[ci].value;

    switch (action) {
        case CONTROL_DECREASE_STEP:
            if (ctrl_mapping[ci].value > ctrl_mapping[ci].minimum) {
                ctrl_mapping[ci].value -= ctrl_mapping[ci].step;
            }
            break;

        case CONTROL_INCREASE_STEP:
            if (ctrl_mapping[ci].value < ctrl_mapping[ci].maximum) {
                ctrl_mapping[ci].value += ctrl_mapping[ci].step;
            }
            break;

        case CONTROL_DESREASE_INCREMENT:
            increment = ctrl_mapping[ci].step * 10;
            if (ctrl_mapping[ci].value - increment < ctrl_mapping[ci].minimum) {
                ctrl_mapping[ci].value = ctrl_mapping[ci].minimum;
            } else {
                ctrl_mapping[ci].value -= increment;
            }
            break;

        case CONTROL_INCREASE_INCREMENT:
            increment = ctrl_mapping[ci].step * 10;
            if (ctrl_mapping[ci].value + increment > ctrl_mapping[ci].maximum) {
                ctrl_mapping[ci].value = ctrl_mapping[ci].maximum;
            } else {
                ctrl_mapping[ci].value += increment;
            }
            break;

        case CONTROL_MINIMUM:
            ctrl_mapping[ci].value = ctrl_mapping[ci].minimum;
            break;

        case CONTROL_MAXIMUM:
            ctrl_mapping[ci].value = ctrl_mapping[ci].maximum;
            break;

        case CONTROL_DEFAULT:
            ctrl_mapping[ci].value = ctrl_mapping[ci].default_value;
            break;
    }

    if (prev_value != ctrl_mapping[ci].value) {
        v4l2_set_ctrl_value(ctrl_mapping[ci].id, ctrl_mapping[ci].value);
        return 1;
    }
    return 0;
}

static void control_reset()
{
    int i;
    for (i = 0; i < ctrl_last; i++){
        ctrl_mapping[i].value = ctrl_mapping[i].default_value;
        v4l2_set_ctrl_value(ctrl_mapping[i].id, ctrl_mapping[i].value);
    }
}

static void control_free()
{
    int i;
    int idx;

    for (i = 0; i < ctrl_last; i++){
        if (ctrl_mapping[i].name) {
            free(ctrl_mapping[i].name);
            ctrl_mapping[i].name = NULL;
        }
        if (ctrl_mapping[i].var_name) {
            free(ctrl_mapping[i].var_name);
            ctrl_mapping[i].var_name = NULL;
        }
        if (ctrl_mapping[i].hasoptions) {
            for (idx = 0; idx < ctrl_mapping[i].maximum - ctrl_mapping[i].minimum; idx++) {
                if (ctrl_mapping[i].options[idx].name) {
                    /* this must be fixed - double free or corruption (out) */
                    // free(ctrl_mapping[i].options[idx].name);
                    ctrl_mapping[i].options[idx].name = NULL;
                }
            }
        }
    }
}

static void control_load()
{
    char name[30];
    int value;
    int i;
    FILE * fp = fopen(config_file, "r");
    
    if (fp != NULL) {
        // Assume control=value file format
        while (fscanf(fp,"%[^=]=%d\r\n", name, &value) == 2) {
            for (i = 0; i < ctrl_last; i++) {
                if (strcmp(name, ctrl_mapping[i].var_name) == 0) {
                    if (ctrl_mapping[i].value != value) {
                        ctrl_mapping[i].value = value;
                        v4l2_set_ctrl_value(ctrl_mapping[i].id, ctrl_mapping[i].value);
                    };
                    break;
                }
            }
        }
        mvprintw(0, 20, "Config file %s loaded", config_file);
        fclose(fp);
    } else {
        mvprintw(0, 20, "Cannot load %s", config_file);
    }
    refresh();
}

static void control_save()
{
    int value;
    // Overwrite existing file
    FILE * fp = fopen(config_file, "w"); 

    if (fp != NULL) {
        for (int i = 0; i < ctrl_last; i++) {
            value = 0;
            
            if (ctrl_mapping[i].value != ctrl_mapping[i].default_value) {
                value = ctrl_mapping[i].value;
                fprintf(fp, "%s=%d\r\n", ctrl_mapping[i].var_name, value);
            }
        }
        fclose(fp);
        mvprintw(0, 20, "Config file %s saved", config_file);
    } else {
        mvprintw(0, 20, "Cannot save %s", config_file);
    }
    refresh();
}

static void menu_item(WINDOW * menu_win, int i, int y, int x)
{
    int idx;
    char * value_diff = " ";

    if (ctrl_mapping[i].value > ctrl_mapping[i].default_value) {
        value_diff = "+";
    } else if (ctrl_mapping[i].value < ctrl_mapping[i].default_value) {
        value_diff = "-";
    }

    /* value */
    mvwprintw(menu_win, y, x, "%46d",
        ctrl_mapping[i].value
    );

    /* option name */
    if (ctrl_mapping[i].hasoptions) {
        for (idx = ctrl_mapping[i].minimum; idx <= ctrl_mapping[i].maximum; idx++) {
            if (ctrl_mapping[i].options[idx].index == ctrl_mapping[i].value) {
                if (ctrl_mapping[i].options[idx].name) {
                    mvwprintw(menu_win, y, x, "%46s", ctrl_mapping[i].options[idx].name);
                } else {
                    mvwprintw(menu_win, y, x, "%46d", ctrl_mapping[i].options[idx].value);
                }
            }
        }
    }

    /* name */
    mvwprintw(menu_win, y, x, "%s %s",
        value_diff,
        ctrl_mapping[i].name
    );
}

static void print_menu(WINDOW * menu_win, int highlight)
{
    int i;
    int x = 2;
    int y = 1;
    int max;
    int offset = 0;
    int btitle_offset = 0;
    int window_lines = WINDOW_HEIGHT - 2;

    if (highlight > window_lines - 1) {
        if (highlight >= last_offset && highlight < last_offset + window_lines) {
            offset = last_offset;

        } else {
            offset = highlight - window_lines + 1;

        }

    } else if (last_offset > 0) {
        if (highlight > last_offset) {
            offset = last_offset;

        } else {
            offset = highlight;

        }

    }
    last_offset = offset;

    max = (ctrl_last >= offset + window_lines) ? offset + window_lines : ctrl_last;

    box(menu_win, 0, 0);
    for (i = offset; i < max; i++) {
        if (highlight == i) {
            wattron(menu_win, A_REVERSE);
            menu_item(menu_win, i, y, x);
            wattroff(menu_win, A_REVERSE);
        } else {
            menu_item(menu_win, i, y, x);
        }
        y++;
    }

    btitle_offset = WINDOW_WIDTH - 9;
    btitle_offset -= (highlight + 1 < 10) ? 1 : ((highlight + 1 < 100) ? 2 : 3);
    btitle_offset -= (ctrl_last < 10) ? 1 : ((ctrl_last < 100) ? 2 : 3);

    mvwhline(menu_win, 0, 1, ACS_HLINE, WINDOW_WIDTH - 2);
    wmove(menu_win, WINDOW_HEIGHT - 1, btitle_offset);
    wprintw(menu_win, "[ %d / %d ]", highlight + 1, ctrl_last);

    wrefresh(menu_win);
}

static void print_control_info(WINDOW * control_win, int cid)
{
    int row = 1;
    int idx;
    werase(control_win);
    box(control_win, 0, 0);
    mvwprintw(control_win, row, 2, "%.23s", ctrl_mapping[cid].name);
    row++;
    mvwprintw(control_win, row, 2, "Val: %18d", ctrl_mapping[cid].value);
    row++;
    mvwprintw(control_win, row, 2, "Min: %18d", ctrl_mapping[cid].minimum);
    row++;
    mvwprintw(control_win, row, 2, "Max: %18d", ctrl_mapping[cid].maximum);
    row++;
    mvwprintw(control_win, row, 2, "Stp: %18d", ctrl_mapping[cid].step);
    row++;
    mvwprintw(control_win, row, 2, "Def: %18d", ctrl_mapping[cid].default_value);
    row++;
    mvwprintw(control_win, row, 2, "Opt: %*s", 18, "");
    if (ctrl_mapping[cid].hasoptions) {
        for (idx = ctrl_mapping[cid].minimum; idx <= ctrl_mapping[cid].maximum; idx++) {
            if (ctrl_mapping[cid].options[idx].index == ctrl_mapping[cid].value) {
                if (ctrl_mapping[cid].options[idx].name) {
                    mvwprintw(control_win, row, 2, "Opt: %18s",
                        ctrl_mapping[cid].options[idx].name
                    );
                } else {
                    mvwprintw(control_win, row, 2, "Opt: %18d",
                        ctrl_mapping[cid].options[idx].value
                    );
                }
            }
        }
    }
    wrefresh(control_win);
}

static void print_info(int row)
{
    int col = WINDOW_WIDTH + 1;
    mvprintw(row, col, "Up/Down/Home/End  Navigate");
    row++;
    mvprintw(row, col, "Left/Right          Adjust");
    row++;
    mvprintw(row, col, "PgDn/PgUp      Jump Adjust");
    row++;
    mvprintw(row, col, "R Reset All");
    row++;
    mvprintw(row, col, "D Default");
    row++;
    mvprintw(row, col, "N Minimum");
    row++;
    mvprintw(row, col, "M Maximum");
    row++;
    mvprintw(row, col, "L Load");
    row++;
    mvprintw(row, col, "S Save");
    row++;
    mvprintw(row, col, "Q Quit");
}

static void print_format_info() 
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(v4l2_dev_fd, VIDIOC_G_FMT, &fmt) < 0) {
        return;
    }
    v4l2_dev_pixelformat = fmt.fmt.pix.pixelformat;

    mvprintw(1, 1, "V4L2:       %s", v4l2_devname);
    mvprintw(2, 1, "Format:     %c%c%c%c", pixfmtstr(fmt.fmt.pix.pixelformat));
    mvprintw(3, 1, "Resolution: %dx%d", fmt.fmt.pix.width, fmt.fmt.pix.height);

    mvprintw(1, 30, "Config: %s", config_file);
}

static int init()
{
    WINDOW * menu_win;
    WINDOW * control_win;
    bool quit = false;
    int c;
    int ret;
    int changed;

    ret = v4l2_open(v4l2_devname);
    if (ret < 0) {
        return 1;
    }

    v4l2_get_controls();

    if (list_controls) {
        goto end;
    }

    curs_set(0);
    initscr();
    clear();
    noecho();
    cbreak();
    
    mvprintw(0, 1, "Camera control: ", v4l2_devname);

    print_format_info();

    menu_win = newwin(WINDOW_HEIGHT, WINDOW_WIDTH, 4, 0);
    keypad(menu_win, TRUE);
    print_info(13);
    
    control_win = newwin(9, 77 - WINDOW_WIDTH, 4, WINDOW_WIDTH + 1);
    refresh();

    print_menu(menu_win, highlight);
    print_control_info(control_win, highlight);
    curs_set(0);

    while(!quit)
    {
        c = wgetch(menu_win);
        refresh();
        changed = 0;
        switch(c) {
            case KEY_UP:
                if (highlight > 0) {
                    highlight -= 1;    
                }
                print_control_info(control_win, highlight);
                break;

            case KEY_DOWN:
                if (highlight < ctrl_last - 1) {
                    highlight += 1;    
                }
                print_control_info(control_win, highlight);
                break;

            case KEY_LEFT:
                changed = control_change(CONTROL_DECREASE_STEP, highlight);
                break;

            case KEY_RIGHT:
                changed = control_change(CONTROL_INCREASE_STEP, highlight);
                break;

            case 338: // Page Down
                changed = control_change(CONTROL_DESREASE_INCREMENT, highlight);
                break;

            case 339: // Page Up
                changed = control_change(CONTROL_INCREASE_INCREMENT, highlight);
                break;

            case 262: // Home
                highlight = 0;
                print_control_info(control_win, highlight);
                break;

            case 360: // END
                highlight = ctrl_last - 1;
                print_control_info(control_win, highlight);
                break;

            case 'N':
            case 'n':
                changed = control_change(CONTROL_MINIMUM, highlight);
                break;

            case 'M':
            case 'm':
                changed = control_change(CONTROL_MAXIMUM, highlight);
                break;

            case 'D':
            case 'd':
                changed = control_change(CONTROL_DEFAULT, highlight);
                break;

            case 'R':
            case 'r':
                control_reset();
                changed = 1;
                break;

            case 'L':
            case 'l':
                mvprintw(0, 20, "%*s", 58, " ");
                if (!DEBUG) {
                    control_load();
                }
                break;

            case 'S':
            case 's':
                mvprintw(0, 20, "%*s", 58, " ");
                if (!DEBUG) {
                    control_save();
                }
                break;

            case 'Q':
            case 'q':
                quit = true;
                break;

            default:
                if (DEBUG) {
                    mvprintw(24, 0, "Character %3d '%c'", c, c);
                    refresh();
                }
                break;
        }
        curs_set(0);
        print_menu(menu_win, highlight);
        if (changed) {
            print_control_info(control_win, highlight);
        }
    }
    curs_set(1);
    clrtoeol();
    refresh();
    endwin();

end:
    v4l2_close();
    control_free();
    return 0;
}

static void usage(const char * argv0)
{
    fprintf(stderr, "Usage: %s [options]\n", argv0);
    fprintf(stderr, "Available options are\n");
    fprintf(stderr, " -c file               Path to config file\n");
    fprintf(stderr, " -d                    Disable unsupported controls\n");
    fprintf(stderr, " -h                    Print this help screen and exit\n");
    fprintf(stderr, " -i control_variable   Ignore control with defined name\n");
    fprintf(stderr, " -l                    List available controls\n");
    fprintf(stderr, " -v device             V4L2 Video Capture device\n");
}

int main(int argc, char * argv[])
{
    int opt;

    while ((opt = getopt(argc, argv, "c:dhi:lv:")) != -1) {
        switch (opt) {
        case 'c':
            config_file = optarg;
            break;

        case 'd':
            disable_unsupported_controls = true;
            break;

        case 'h':
            usage(argv[0]);
            return 1;

        case 'i':
            if (last_ignored_variable < 49) {
                printf("INFO: Ignored control: %s\n", optarg);
                ignored_variables[last_ignored_variable] = optarg;
                last_ignored_variable += 1;
            }
            break;

        case 'l':
            list_controls = true;
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