#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/inotify.h>
#include <sys/param.h>

#include "sys.h"
#include "script.h"
#include "pwindow.h"
#include "bytes.h"
#include "debug.h"
#include "xalloc.h"
#include "thread.h"

#include <PalmOS.h>
#include "pumpkin.h"

typedef struct {
  int width, height, depth, pitch;
  int fb_fd, kbd_fd, mouse_fd, notify_fd, len;
  int x, y;
  uint32_t *offscreen;
  void *p;
  int key_shift, key_ctrl, key_sym;
  int key_is_down, clear_shift, clear_ctrl, clear_sym;
} display_t;

struct texture_t {
  int width, height, size;
  uint32_t *pixels;
};

#define TAG_BATTERY_MONITOR  "battery_monitor"
#define MAX_PATH 256

typedef struct {
  int window_count;
  int battery_thread_handle;
  char app_install_path[MAX_PATH];
} beepy_state_t;

static window_provider_t wp;
static beepy_state_t beepy_state;

static void put_pixel(int x, int y, uint32_t color, display_t *display) {
  if ((x < 0) || (x >= display->width) || (y < 0) || (y >= display->height)) {
    return;
  }
  // calculate the pixel's byte offset inside the buffer
  unsigned int pix_offset = ((x * display->depth / 8) + (y * display->pitch));
  *((uint32_t*)(display->p + pix_offset)) = color;
}

#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))
// Ascii Pointer:
static const int pointer_offset_x = -2;
static const int pointer_offset_y = -1;
static const char pointer[] =
" ...\n\
..#..\n\
..##..\n\
..###..\n\
..####..\n\
..#####..\n\
..######..\n\
..#######..\n\
..########..\n\
..#########..\n\
..##########..\n\
..###########..\n\
..############..\n\
..#############..\n\
..##############..\n\
..###############..\n\
..################..\n\
..######...........\n\
..#####..\n\
..####..\n\
..###..\n\
..##..\n\
..#..\n\
 ...";

static void render_to_display(display_t *display) {
  memcpy(display->p, display->offscreen, display->len);
  // copy the ascii-art pointer to the display as pixels
  int x = 0;
  int y = 0;
  for(int i = 0; i < COUNT_OF(pointer); i++) {
    if (pointer[i] == '#') put_pixel(display->x + x + pointer_offset_x, display->y + y + pointer_offset_y, 0, display);
    if (pointer[i] == '.') put_pixel(display->x + x + pointer_offset_x, display->y + y + pointer_offset_y, 0xFFFFFFFF, display);
    x += 1;
    if (pointer[i] == '\n') {
      x = 0;
      y += 1;
    }
  }
}

static window_t *window_create(int encoding, int *width, int *height, int xfactor, int yfactor, int rotate, int fullscreen, int software, void *data) {
  display_t *display = xmalloc(sizeof(display_t));
  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;
  int fb_fd, kbd_fd, mouse_fd, notify_fd;
  void *p;
  window_t *w;

  if (beepy_state.window_count == 0) {
    fb_fd = open("/dev/fb1", O_RDWR);
    if (fb_fd == -1) debug_errno("BEEPY", "open fb");

    kbd_fd = open("/dev/input/event0", O_RDONLY);
    if (kbd_fd == -1) debug_errno("BEEPY", "open keyboard");
    ioctl(kbd_fd, EVIOCGRAB, 1);

    mouse_fd = open("/dev/input/event1", O_RDONLY);
    if (mouse_fd == -1) debug_errno("BEEPY", "open mouse");

    notify_fd = inotify_init1(IN_NONBLOCK);
    if (notify_fd == -1) debug_errno("BEEPY", "open file notify");

    if (fb_fd != -1 && kbd_fd != -1 && mouse_fd != -1) {
      debug(DEBUG_INFO, "BEEPY", "framebuffer fb1 open as fd %d", fb_fd);
      debug(DEBUG_INFO, "BEEPY", "keyboard event0 open as fd %d", kbd_fd);
      debug(DEBUG_INFO, "BEEPY", "mouse event1 open as fd %d", mouse_fd);
      debug(DEBUG_INFO, "BEEPY", "file notify open as fd %d", notify_fd);

      if (notify_fd != -1 && beepy_state.app_install_path[0] != 0) {
        int wd = inotify_add_watch(notify_fd, beepy_state.app_install_path, IN_CREATE);
        if (wd == -1) {
          debug(DEBUG_ERROR, "BEEPY", "Unable to notify for changes in '%s'", beepy_state.app_install_path);
          close(notify_fd);
          notify_fd = 0;
        }
      }

      if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) != -1 &&
          ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) != -1) {
        debug(DEBUG_INFO, "BEEPY", "framebuffer %s %dx%d bpp %d type %d", finfo.id, vinfo.xres, vinfo.yres, vinfo.bits_per_pixel, finfo.type);

        if ((p = (uint16_t *)mmap(0, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0)) != NULL) {
          display->fb_fd = fb_fd;
          display->kbd_fd = kbd_fd;
          display->mouse_fd = mouse_fd;
          display->notify_fd = notify_fd;
          display->p = p;
          display->offscreen = xmalloc(finfo.smem_len);
          display->width = vinfo.xres;
          display->height = vinfo.yres;
          display->x = display->width / 2;
          display->y = display->height / 2;
          display->depth = vinfo.bits_per_pixel;
          display->pitch = finfo.line_length;
          display->len = finfo.smem_len;
          *width = display->width;
          *height = display->height;
          w = (window_t *)display;
          beepy_state.window_count = 1;
        }
      } else {
        debug_errno("BEEPY", "ioctl");
        close(fb_fd);
        close(kbd_fd);
        close(mouse_fd);
        if (notify_fd != -1) close(notify_fd);
      }
    } else {
      if (fb_fd != -1) close(fb_fd);
      if (kbd_fd != -1) close(kbd_fd);
      if (mouse_fd != -1) close(mouse_fd);
      if (notify_fd != -1) close(notify_fd);
    }
  } else {
    debug(DEBUG_ERROR, "BEEPY", "only one window can be created");
    w = NULL;
  }

  return w;
}

static int window_destroy(window_t *window) {
  display_t *display = (display_t *)window;

  if (display) {
    if (display->fb_fd > 0) {
      munmap(display->p, display->len);
      close(display->fb_fd);
    }
    if (display->kbd_fd > 0) {
      close(display->kbd_fd);
    }
    if (display->mouse_fd > 0) {
      close(display->mouse_fd);
    }
    if (display->notify_fd > 0) {
      close(display->notify_fd);
    }
    if (display->offscreen) xfree(display->offscreen);

    xfree(display);
    beepy_state.window_count = 0;
  }

  return 0;
}

static texture_t *window_create_texture(window_t *window, int width, int height) {
  texture_t *texture;

  if ((texture = xcalloc(1, sizeof(texture_t))) != NULL) {
    texture->width = width;
    texture->height = height;
    texture->size = width * height * sizeof(uint32_t);
    texture->pixels = xmalloc(texture->size);
    if (texture->pixels == NULL) {
      xfree(texture);
      return NULL;
    }
  }

  return texture;
}

static int window_destroy_texture(window_t *window, texture_t *texture) {
  if (texture) {
    xfree(texture->pixels);
    xfree(texture);
  }

  return 0;
}

static int window_update_texture_rect(window_t *window, texture_t *texture, uint8_t *src, int tx, int ty, int w, int h) {
  int i, j, tpitch, tindex;
  uint32_t *b32;

  if (texture && src) {
    tpitch = texture->width;
    tindex = ty * tpitch + tx;
    b32 = (uint32_t *)src;

    for (i = 0; i < h; i++) {
      for (j = 0; j < w; j++) {
        texture->pixels[tindex + j] = b32[tindex + j];
      }
      tindex += tpitch;
    }
  }

  return 0;
}

static int window_update_texture(window_t *window, texture_t *texture, uint8_t *raw) {
  if (texture && raw) {
    xmemcpy(texture->pixels, raw, texture->size);
  }

  return 0;
}

static int window_draw_texture_rect(window_t *window, texture_t *texture, int tx, int ty, int w, int h, int x, int y) {
  display_t *display = (display_t *)window;
  int i, j, d, tpitch, wpitch, tindex, windex, r = -1;

  if (texture && display->p) {
    if (x < 0) {
      tx -= x;
      w += x;
      x = 0;
    }
    if (x+w >= display->width) {
      d = x+w - display->width;
      w -= d;
    }
    if (y < 0) {
      ty -= y;
      h += y;
      y = 0;
    }
    if (y+h >= display->height) {
      d = y+h - display->height;
      h -= d;
    }

    if (w > 0 && h > 0) {
      tpitch = texture->width;
      wpitch = display->width;
      tindex = ty * tpitch + tx;
      windex = y * wpitch + x;

      for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
          display->offscreen[windex + j] = texture->pixels[tindex + j];
        }
        tindex += tpitch;
        windex += wpitch;
      }
      render_to_display(display);
      r = 0;
    }
  }

  return r;
}

static int window_draw_texture(window_t *window, texture_t *texture, int x, int y) {
  int r = -1;

  if (texture) {
    r = window_draw_texture_rect(window, texture, 0, 0, texture->width, texture->height, x, y);
  }

  return r;
}

static int map_keycode(uint16_t code, display_t *d) {
  int key = 0;
  debug(DEBUG_TRACE, "BEEPY", "CTRL: %d, SHIFT: %d, SYM: %d, code: %d", d->key_ctrl, d->key_shift, d->key_sym, code);
  if (d->key_ctrl) {
    switch (code) {
      case KEY_CONFIG: key = WINDOW_KEY_HOME; break;

      case KEY_Q: key = 17; break;
      case KEY_W: key = 23; break;
      case KEY_E: key = 5; break;
      case KEY_R: key = 18; break;
      case KEY_T: key = 20; break;
      case KEY_Y: key = 25; break;
      case KEY_U: key = 21; break;
      case KEY_I: key = 9; break;
      case KEY_O: key = 15; break;
      case KEY_P: key = 16; break;

      case KEY_A: key = 1; break;
      case KEY_S: key = 19; break;
      case KEY_D: key = 4; break;
      case KEY_F: key = 6; break;
      case KEY_G: key = 7; break;
      case KEY_H: key = 8; break;
      case KEY_J: key = 10; break;
      case KEY_K: key = 11; break;
      case KEY_L: key = 12; break;

      case KEY_Z: key = 26; break;
      case KEY_X: key = 24; break;
      case KEY_C: key = 3; break;
      case KEY_V: key = 22; break;
      case KEY_B: key = 2; break;
      case KEY_N: key = 14; break;
      case KEY_M: key = 13; break;
    }
  } else if (d->key_sym) {
    switch (code) {
      case 16: key = '~'; break;
      case 17: key = '`'; break;
      case 18: key = '{'; break;
      case 19: key = '}'; break;
      case 20: key = '['; break;
      case 21: key = ']'; break;
      case 22: key = '<'; break;
      case 23: key = '>'; break;
      case 24: key = '^'; break;
      case 25: key = '%'; break;

      case 30: key = '='; break;
      case 31: key = 246; break; // division sign
      case 32: key = 241; break; // plus/minus
      case 33: key = 249; break; // period centered
      case 34: key = '\\'; break;
      case 35: key = '|'; break;
      case 36: key = '&'; break;
      case 37: key = '"'; break;
      case 38: key = '"'; break;

      case 44: key = 157; break; // yen
      case 45: key = 238; break; // euro
      case 46: key = 156; break; // sterling
      case 47: key = 168; break; // upside down question
      case 48: key = 173; break; // upside down exclamation
      case 49: key = 174; break;
      case 50: key = 175; break;
      case 113: key = '$'; break;
    }
  } else {
    switch (code) {
      case KEY_1: key = '1'; break;
      case KEY_2: key = '2'; break;
      case KEY_3: key = '3'; break;
      case KEY_4: key = '4'; break;
      case KEY_5: key = '5'; break;
      case KEY_6: key = '6'; break;
      case KEY_7: key = '7'; break;
      case KEY_8: key = '8'; break;
      case KEY_9: key = '9'; break;
      case KEY_0: key = '0'; break;

      case KEY_ESC: key = 27; break;
      case KEY_MINUS: key = '-'; break;
      case KEY_EQUAL: key = '='; break;
      case KEY_BACKSPACE: key = '\b'; break;
      case KEY_TAB: key = '\t'; break;
      case KEY_LEFTBRACE: key = '{'; break;
      case KEY_RIGHTBRACE: key = '}'; break;
      case KEY_ENTER: key = '\n'; break;
      case KEY_SEMICOLON: key = ';'; break;
      case KEY_APOSTROPHE: key = '\''; break;
      case KEY_BACKSLASH: key = '\\'; break;
      case KEY_SPACE: key = ' '; break;

      case KEY_A: key = d->key_shift ? 'A' : 'a'; break;
      case KEY_B: key = d->key_shift ? 'B' : 'b'; break;
      case KEY_C: key = d->key_shift ? 'C' : 'c'; break;
      case KEY_D: key = d->key_shift ? 'D' : 'd'; break;
      case KEY_E: key = d->key_shift ? 'E' : 'e'; break;
      case KEY_F: key = d->key_shift ? 'F' : 'f'; break;
      case KEY_G: key = d->key_shift ? 'G' : 'g'; break;
      case KEY_H: key = d->key_shift ? 'H' : 'h'; break;
      case KEY_I: key = d->key_shift ? 'I' : 'i'; break;
      case KEY_J: key = d->key_shift ? 'J' : 'j'; break;
      case KEY_K: key = d->key_shift ? 'K' : 'k'; break;
      case KEY_L: key = d->key_shift ? 'L' : 'l'; break;
      case KEY_M: key = d->key_shift ? 'M' : 'm'; break;
      case KEY_N: key = d->key_shift ? 'N' : 'n'; break;
      case KEY_O: key = d->key_shift ? 'O' : 'o'; break;
      case KEY_P: key = d->key_shift ? 'P' : 'p'; break;
      case KEY_Q: key = d->key_shift ? 'Q' : 'q'; break;
      case KEY_R: key = d->key_shift ? 'R' : 'r'; break;
      case KEY_S: key = d->key_shift ? 'S' : 's'; break;
      case KEY_T: key = d->key_shift ? 'T' : 't'; break;
      case KEY_U: key = d->key_shift ? 'U' : 'u'; break;
      case KEY_V: key = d->key_shift ? 'V' : 'v'; break;
      case KEY_W: key = d->key_shift ? 'W' : 'w'; break;
      case KEY_X: key = d->key_shift ? 'X' : 'x'; break;
      case KEY_Y: key = d->key_shift ? 'Y' : 'y'; break;
      case KEY_Z: key = d->key_shift ? 'Z' : 'z'; break;

      case KEY_PASTE: key = '#'; break;
      case KEY_FIND: key = '1'; break;
      case KEY_CUT: key = '2'; break;
      case KEY_HELP: key = '3'; break;
      case KEY_MENU: key = '('; break;
      case KEY_CALC: key = ')'; break;
      case KEY_SETUP: key = '_'; break;
      case KEY_SLEEP: key = '-'; break;
      case KEY_WAKEUP: key = '+'; break;
      case KEY_FILE: key = '@'; break;

      case KEY_PROG2: key = '*'; break;
      case KEY_WWW: key = '4'; break;
      case KEY_MSDOS: key = '5'; break;
      case KEY_COFFEE: key = '6'; break;
      case KEY_DIRECTION: key = '/'; break;
      case KEY_CYCLEWINDOWS: key = ':'; break;
      case KEY_MAIL: key = ';'; break;
      case KEY_BOOKMARKS: key = '\''; break;
      case KEY_COMPUTER: key = '"'; break;
      case KEY_COPY: key = '\b'; break;

      case KEY_NEXTSONG: key = '7'; break;
      case KEY_PLAYPAUSE: key = '8'; break;
      case KEY_PREVIOUSSONG: key = '9'; break;
      case KEY_STOPCD: key = '?'; break;
      case KEY_RECORD: key = '!'; break;
      case KEY_REWIND: key = ','; break;
      case KEY_PHONE: key = '.'; break;
      case KEY_XFER: key = '\t'; break;

      case KEY_PROPS: key = '0'; break;
      case KEY_MUTE: key = WINDOW_KEY_F5; break;
    }
  }
  if (key == 0) {
    debug(DEBUG_ERROR, "BEEPY", "map_keycode: %d (%04X) -> UNHANDLED)", code, code);
  } else {
    debug(DEBUG_TRACE, "BEEPY", "map_keycode: %d (%04X) -> %d)", code, code, key);
  }
  return key;
}

static int window_event2(window_t *window, int wait, int *arg1, int *arg2) {
  display_t *display = (display_t *)window;
  struct input_event event;
  struct timeval tv;
  fd_set fds;
  int nfds, nread, ev = -1;

  for (; ev == -1;) {
    FD_ZERO(&fds);
    FD_SET(display->kbd_fd, &fds);
    FD_SET(display->mouse_fd, &fds);
    if (display->notify_fd > 0) FD_SET(display->notify_fd, &fds);
    nfds = MAX(display->kbd_fd, MAX(display->mouse_fd, display->notify_fd));
    tv.tv_sec = 0;
    tv.tv_usec = wait;

    switch (select(nfds+1, &fds, NULL, NULL, &tv)) {
      case -1:
        debug(DEBUG_ERROR, "BEEPY", "input_event error");
        return -1;

      case 0:
        ev = 0;
        break;

      default:
        nread = 0;
        if (FD_ISSET(display->kbd_fd, &fds)) {
          sys_read_timeout(display->kbd_fd, (uint8_t *)&event, sizeof(event), &nread, 0);
        } else if (FD_ISSET(display->mouse_fd, &fds)) {
          sys_read_timeout(display->mouse_fd, (uint8_t *)&event, sizeof(event), &nread, 0);
        } else {
          char buf[4096];
          nread = read(display->notify_fd, buf, sizeof(buf));
          if (nread <= 0) break;
          const struct inotify_event *notify_event;
          for (char *ptr = buf; ptr < buf + nread; ptr += sizeof(struct inotify_event) + notify_event->len) {
            notify_event = (const struct inotify_event *)ptr;
            debug(DEBUG_INFO, "BEEPY", "Installing %s", notify_event->name);
          }
          debug(DEBUG_TRACE, "BEEPY", "send MSG_DEPLOY");
          client_request_t creq;
          xmemset(&creq, 0, sizeof(client_request_t));
          creq.type = MSG_DEPLOY;
          thread_client_write(pumpkin_get_spawner(), (uint8_t *)&creq, sizeof(client_request_t));
          break;
        }
        debug(DEBUG_TRACE, "BEEPY", "read %d bytes", nread);

        if (nread == sizeof(event)) {
          debug(DEBUG_TRACE, "BEEPY", "type %u code %u value %u", event.type, event.code, event.value);

          // types and codes defined in /usr/include/linux/input-event-codes.h
          switch (event.type) {
            case EV_KEY:
              debug(DEBUG_TRACE, "BEEPY", "EV_KEY 0x%04X down=%d", event.code, event.value);
              switch (event.code) {
                case BTN_MOUSE: // BTN_LEFT (for mouse)
                  ev = (event.value == 1) ? WINDOW_BUTTONDOWN : WINDOW_BUTTONUP;
                  *arg1 = 1;
                  *arg2 = 0;
                  break;
                case KEY_LEFTSHIFT:
                  debug(DEBUG_TRACE, "BEEPY", "SHIFT key: %d", event.value);
                  if (event.value == 1) {
                    display->key_shift = 1;
                  } else if (display->key_is_down) {
                    display->clear_shift = 1;
                  } else {
                    display->key_shift = 0;
                  }
                  ev = (event.value == 1) ? WINDOW_KEYDOWN : WINDOW_KEYUP;
                  *arg1 = WINDOW_KEY_SHIFT;
                  *arg2 = 0;
                  break;
                case KEY_LEFTCTRL:
                  debug(DEBUG_TRACE, "BEEPY", "CTRL key: %d", event.value);
                  if (event.value == 1) {
                    display->key_ctrl = 1;
                  } else if (display->key_is_down) {
                    display->clear_ctrl = 1;
                  } else {
                    display->key_ctrl = 0;
                  }
                  ev = (event.value == 1) ? WINDOW_KEYDOWN : WINDOW_KEYUP;
                  *arg1 = WINDOW_KEY_CTRL;
                  *arg2 = 0;
                  break;
                case KEY_RIGHTALT:
                  debug(DEBUG_TRACE, "BEEPY", "SYM key: %d", event.value);
                  if (event.value == 1) {
                    display->key_sym = 1;
                  } else if (display->key_is_down) {
                    display->clear_sym = 1;
                  } else {
                    display->key_sym = 0;
                  }
                  ev = (event.value == 1) ? WINDOW_KEYDOWN : WINDOW_KEYUP;
                  *arg1 = WINDOW_KEY_RALT;
                  *arg2 = 0;
                  break;
                default:
                  ev = (event.value == 1) ? WINDOW_KEYDOWN : WINDOW_KEYUP;
                  *arg1 = map_keycode(event.code, display);
                  *arg2 = 0;
                  display->key_is_down = event.value;
                  if (display->clear_shift && display->key_is_down == 0) {
                    display->key_shift = 0;
                    display->clear_shift = 0;
                  }
                  if (display->clear_ctrl && display->key_is_down == 0) {
                    display->key_ctrl = 0;
                    display->clear_ctrl = 0;
                  }
                  if (display->clear_sym && display->key_is_down == 0) {
                    display->key_sym = 0;
                    display->clear_sym = 0;
                  }
              }
              break;

            case EV_REL:
              if (event.code == REL_X) {
                int prev = display->x;
                display->x += event.value;
                if (display->x < 0) display->x = 0;
                else if (display->x >= display->width) display->x = display->width - 1;
                debug(DEBUG_TRACE, "BEEPY", "EV_REL X %d: %d -> %d", event.value, prev, display->x);
              } else if (event.code == REL_Y) {
                int prev = display->y;
                display->y += event.value;
                if (display->y < 0) display->y = 0;
                else if (display->y >= display->height) display->y = display->height - 1;
                debug(DEBUG_TRACE, "BEEPY", "EV_REL Y %d: %d -> %d", event.value, prev, display->y);
              }
              render_to_display(display);
              ev = WINDOW_MOTION;
              *arg1 = display->x;
              *arg2 = display->y;
              break;
            }
        }
        break;
    }
  }

  if (ev) debug(DEBUG_TRACE, "BEEPY", "window_event event %d arg1=%d arg2=%d", ev, *arg1, *arg2);

  return ev;
}

static int read_sysfs(char *path, uint8_t *buffer, int len) {
  int nread;
  int fd;

  xmemset(buffer, 0, len);
  fd = open(path, O_RDONLY);
  if (fd == -1) {
    debug(DEBUG_ERROR, "BEEPY", "Unable to open sysfs %s", path);
    return -1;
  }
  sys_read_timeout(fd, buffer, len, &nread, 0);
  debug(DEBUG_TRACE, "BEEPY", "read_sysfs read %d bytes", nread);
  close(fd);
  return 0;
}

static int battery_level() {
  char buffer[12];
  int battery;

  if (read_sysfs("/sys/firmware/beepy/battery_percent", (uint8_t *)buffer, 12) == 0) {
    battery = sys_atoi(buffer);
    pumpkin_set_battery(battery);
    debug(DEBUG_TRACE, "BEEPY", "set current battery: %d", battery);
  } else {
    battery = 100;
    debug(DEBUG_ERROR, "BEEPY", "unable to read battery level");
  }

  return battery;
}

static int monitor_action(void *arg) {
  int count = 0;
  const int sleep_time = 500000; // 0.5 seconds
  const int check_secs = 3 * 1000000 / sleep_time; // every 3 seconds

  for (; !thread_must_end();) {
    if (count == 0) {
      battery_level();
    }
    count = (count + 1) % check_secs;
    sys_usleep(sleep_time);
  }

  return 0;
}

static int libbeepy_start(int pe) {
  int r = 0;
  char *vfs_root = NULL;

  if (script_get_string(pe, 0, &vfs_root) == 0) {
    sys_sprintf(beepy_state.app_install_path, "%sapp_install", vfs_root);
  }

  debug(DEBUG_INFO, "BEEPY", "start: '%s'", beepy_state.app_install_path);
  if ((beepy_state.battery_thread_handle = thread_begin(TAG_BATTERY_MONITOR, monitor_action, NULL)) == -1) {
    debug(DEBUG_ERROR, "BEEPY", "Failed to start battery monitor thread");
    r = -1;
  }

  return r;
}

static int libbeepy_get_battery(int pe) {
  return script_push_integer(pe, battery_level());
}

static int libbeepy_finish(int pe) {
  int r = 0;

  debug(DEBUG_INFO, "BEEPY", "finish");
  if (beepy_state.battery_thread_handle != -1) {
    r = thread_end(TAG_BATTERY_MONITOR, beepy_state.battery_thread_handle);
    beepy_state.battery_thread_handle = -1;
  }

  return r;
}

int libbeepy_init(int pe, script_ref_t obj) {
  xmemset(&beepy_state, 0, sizeof(beepy_state_t));
  beepy_state.window_count = 0;

  xmemset(&wp, 0, sizeof(window_provider_t));
  wp.create = window_create;
  wp.destroy = window_destroy;
  wp.create_texture = window_create_texture;
  wp.destroy_texture = window_destroy_texture;
  wp.update_texture = window_update_texture;
  wp.draw_texture = window_draw_texture;
  wp.update_texture_rect = window_update_texture_rect;
  wp.draw_texture_rect = window_draw_texture_rect;
  wp.event2 = window_event2;

  debug(DEBUG_INFO, "BEEPY", "registering provider %s", WINDOW_PROVIDER);
  script_set_pointer(pe, WINDOW_PROVIDER, &wp);

  script_add_function(pe, obj, "start", libbeepy_start);
  script_add_function(pe, obj, "battery_level", libbeepy_get_battery);
  script_add_function(pe, obj, "finish", libbeepy_finish);

  return 0;
}
