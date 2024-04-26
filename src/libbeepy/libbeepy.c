#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input-event-codes.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>

#include "sys.h"
#include "script.h"
#include "pwindow.h"
#include "bytes.h"
#include "debug.h"
#include "xalloc.h"

typedef struct {
  int width, height, depth, pitch;
  int fb_fd, kbd_fd, mouse_fd, len;
  int x, y, buttons, button_down;
  uint32_t *offscreen;
  void *p;
} display_t;

struct texture_t {
  int width, height, size;
  uint32_t *pixels;
};

static window_provider_t wp;
static int window_count = 0;

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
static char pointer[] =
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
..######..\n\
..#####..\n\
..####..\n\
..###..\n\
..##..\n\
..#..\n\
 ...";

static void draw_pointer(display_t *display) {
  memcpy(display->p, display->offscreen, display->len);
  // copy the ascii-art pointer to the display as pixels
  int x = 0;
  int y = 0;
  for(int i = 0; i < COUNT_OF(pointer); i++) {
    if (pointer[i] == '#') put_pixel(display->x + x, display->y + y, 0, display);
    if (pointer[i] == '.') put_pixel(display->x + x, display->y + y, 0xFFFFFFFF, display);
    x += 1;
    if (pointer[i] == '\n') {
      x = 0;
      y += 1;
    }
  }
}

static int input_event(display_t *display, uint32_t us, int *x, int *y, int *button, int* keyCode) {
  uint8_t buf[24];
  uint32_t value;
  int32_t ivalue;
  uint16_t type, code;
  struct timeval tv;
  fd_set fds;
  int i, nfds, len, nread, hast, hasx, hasy, down, ev = -1;

  hast = hasx = hasy = down = 0;
  len = sizeof(struct timeval) + 8; // struct timeval can be 16 bytes or 24 bytes
  *x = 0;
  *y = 0;
  *button = 0;

  for (; ev == -1;) {
    FD_ZERO(&fds);
    FD_SET(display->kbd_fd, &fds);
    FD_SET(display->mouse_fd, &fds);
    nfds = display->kbd_fd > display->mouse_fd ? display->kbd_fd : display->mouse_fd;
    tv.tv_sec = 0;
    tv.tv_usec = us;

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
          sys_read_timeout(display->kbd_fd, buf, len, &nread, 0);
        } else {
          sys_read_timeout(display->mouse_fd, buf, len, &nread, 0);
        }
        debug(DEBUG_TRACE, "BEEPY", "read %d bytes", nread);

        if (nread == len) {
          i = len - 8; // ignore struct timeval
          i += get2l(&type, buf, i);
          i += get2l(&code, buf, i);
          i += get4l(&value, buf, i);
          debug(DEBUG_TRACE, "BEEPY", "type %u code %u value %u", type, code, value);

          // types and codes defined in /usr/include/linux/input-event-codes.h
          switch (type) {
            case 0x00: // EV_SYN
              if (hast) {
                ev = down ? WINDOW_BUTTONDOWN : WINDOW_BUTTONUP;
                *x = display->x;
                *y = display->y;
              } else if (hasx || hasy) {
                ev = WINDOW_MOTION;
                *x = display->x;
                *y = display->y;
              } else {
                ev = 0;
              }
              debug(DEBUG_TRACE, "BEEPY", "EV_SYN event %d x=%d y=%d", ev, display->x, display->y);
              break;
            case 0x01: // EV_KEY
              debug(DEBUG_TRACE, "BEEPY", "EV_KEY 0x%04X down=%d", code, value);
              switch (code) {
                case 0x110: // BTN_LEFT (for mouse)
                case 0x14A: // BTN_TOUCH (for touch screen)
                  *button = 1;
                  down = value ? 1 : 0;
                  hast = 1;
                  break;
                case 0x111: // BTN_RIGHT (for mouse)
                  *button = 2;
                  down = value ? 1 : 0;
                  hast = 1;
                  break;
                case KEY_ESC:
                  ev = value ? WINDOW_KEYDOWN : WINDOW_KEYUP;
                  *keyCode = WINDOW_KEY_HOME;
                  break;
              }
              break;
            case 0x02: // EV_REL
                ivalue = value;
                switch (code) {
                  case 0x00: // REL_X
                    debug(DEBUG_TRACE, "BEEPY", "EV_REL X %d: %d -> %d", ivalue, display->x, display->x + ivalue);
                    display->x += ivalue;
                    if (display->x < 0) display->x = 0;
                    else if (display->x >= display->width) display->x = display->width-1;
                    hasx = 1;
                    break;
                  case 0x01: // REL_Y
                    debug(DEBUG_TRACE, "BEEPY", "EV_REL Y %d: %d -> %d", ivalue, display->y, display->y + ivalue);
                    display->y += ivalue;
                    if (display->y < 0) display->y = 0;
                    else if (display->y >= display->height) display->y = display->height-1;
                    hasy = 1;
                    break;
                }
                draw_pointer(display);
                break;
            }
        }
        break;
    }
  }

  return ev;
}

static window_t *window_create(int encoding, int *width, int *height, int xfactor, int yfactor, int rotate, int fullscreen, int software, void *data) {
  display_t *display = xmalloc(sizeof(display_t));
  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;
  int fb_fd, kbd_fd, mouse_fd;
  void *p;
  window_t *w;

  if (window_count == 0) {
    fb_fd = open("/dev/fb1", O_RDWR);
    if (fb_fd == -1) debug_errno("BEEPY", "open fb");

    kbd_fd = open("/dev/input/event0", O_RDONLY);
    if (kbd_fd == -1) debug_errno("BEEPY", "open keyboard");

    mouse_fd = open("/dev/input/event1", O_RDONLY);
    if (mouse_fd == -1) debug_errno("BEEPY", "open mouse");

    if (fb_fd != -1 && kbd_fd != -1 && mouse_fd != -1) {
      debug(DEBUG_INFO, "BEEPY", "framebuffer fb1 open as fd %d", fb_fd);
      debug(DEBUG_INFO, "BEEPY", "keyboard event0 open as fd %d", kbd_fd);
      debug(DEBUG_INFO, "BEEPY", "mouse event1 open as fd %d", mouse_fd);

      if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) != -1 &&
          ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) != -1) {
        debug(DEBUG_INFO, "BEEPY", "framebuffer %s %dx%d bpp %d type %d", finfo.id, vinfo.xres, vinfo.yres, vinfo.bits_per_pixel, finfo.type);

        if ((p = (uint16_t *)mmap(0, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0)) != NULL) {
          display->fb_fd = fb_fd;
          display->kbd_fd = kbd_fd;
          display->mouse_fd = mouse_fd;
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
          window_count = 1;
        }
      } else {
        debug_errno("BEEPY", "ioctl");
        close(fb_fd);
        close(kbd_fd);
        close(mouse_fd);
      }
    } else {
      if (fb_fd != -1) close(fb_fd);
      if (kbd_fd != -1) close(kbd_fd);
      if (mouse_fd != -1) close(mouse_fd);
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
    if (display->offscreen) xfree(display->offscreen);

    xfree(display);
    window_count = 0;
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
      draw_pointer(display);
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

static int window_event2(window_t *window, int wait, int *arg1, int *arg2) {
  display_t *display = (display_t *)window;
  int button, code, ev = 0;

  if (display->kbd_fd > 0) {
    if (display->button_down) {
      debug(DEBUG_TRACE, "BEEPY", "restoring button down");
      ev = WINDOW_BUTTONDOWN;
      *arg1 = display->button_down;
      *arg2 = 0;
      display->button_down = 0;
    } else {
      ev = input_event(display, wait, arg1, arg2, &button, &code);
      switch (ev) {
        case WINDOW_BUTTONDOWN:
          debug(DEBUG_TRACE, "BEEPY", "changing first button down into motion");
          display->button_down = button;
          ev = WINDOW_MOTION;
          break;
        case WINDOW_BUTTONUP:
          *arg1 = button;
          *arg2 = 0;
          break;
        case WINDOW_KEYDOWN:
        case WINDOW_KEYUP:
          *arg1 = code;
          *arg2 = 0;
          break;
      }
    }
  }
  if (ev) debug(DEBUG_TRACE, "BEEPY", "window_event event %d x=%d y=%d", ev, *arg1, *arg2);

  return ev;
}

static void window_status(window_t *window, int *x, int *y, int *buttons) {
  display_t *display = (display_t *)window;
  *x = display->x;
  *y = display->y;
  *buttons = display->buttons;
}

int libbeepy_init(int pe, script_ref_t obj) {
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
  wp.status = window_status;

  debug(DEBUG_INFO, "BEEPY", "registering provider %s", WINDOW_PROVIDER);
  script_set_pointer(pe, WINDOW_PROVIDER, &wp);

  return 0;
}
