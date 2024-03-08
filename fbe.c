#define PROGNAME "fbe"
#define VERSION "0.1.0"
#define FILE_FBE_BUFFER "/dev/shm/fbe_buffer"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

void X11_initialize();
void fbe_loop();

Display *display;
Colormap colormap;
GC gc;
Window window, root, parent;
int depth, screen, visibility;

char *host = NULL, *cmapbuf;

int CRTX = 800;
int CRTY = 480;
int CRTX_TOTAL = 800;
int BITS_PER_PIXEL = 16;
int lcd_look = 0;

int PIXELS_PER_LONG;
int fd_touch;

#define MAX_CRTX 800
#define MAX_CRTY 480

unsigned long *crtbuf;

#define CHUNKX 32
#define CHUNKY 40

unsigned long crcs[MAX_CRTX / CHUNKX][MAX_CRTY / CHUNKY];

void fbe_init(void *crtbuf_) {
  if (host == NULL)
    if ((host = (char *)getenv("DISPLAY")) == NULL) {
      fprintf(stderr, "%s", "Error: No environment variable DISPLAY\n");
    }

  int PIXELS_PER_BYTE = (16 / BITS_PER_PIXEL);
  PIXELS_PER_LONG = PIXELS_PER_BYTE * 4;
  X11_initialize();

  gc = XCreateGC(display, window, 0, NULL);
  crtbuf = crtbuf_;
  fbe_loop();
}

void X11_initialize() {
  if ((display = XOpenDisplay(host)) == NULL) {
    fprintf(stderr, "Error: Connection could not be made.\n");
    exit(1);
  }

  screen = DefaultScreen(display);
  colormap = DefaultColormap(display, screen);
  parent = root = RootWindow(display, screen);
  depth = DefaultDepth(display, screen);

  XSelectInput(display, root, SubstructureNotifyMask);

  XSetWindowAttributes attr;
  attr.event_mask = ExposureMask;

  attr.background_pixel = BlackPixel(display, screen);

  {
    window = XCreateWindow(display, root, 0, 0, CRTX, CRTY, 0, depth,
                           InputOutput, DefaultVisual(display, screen),
                           CWEventMask | CWBackPixel, &attr);

    char name_[80];
    // sprintf(name_, "bsm-emulator %d * %d * %d bpp", CRTX, CRTY,
    // BITS_PER_PIXEL);
    sprintf(name_, "bms-emulator");
    int name_len = strlen(name_);               // compiler warning workaround
    const unsigned char *name = (void *)name_;  // compiler warning workaround
    XChangeProperty(display, window, XA_WM_NAME, XA_STRING, 8, PropModeReplace,
                    name, name_len);
    XMapWindow(display, window);
  }

  XWMHints xwmhints;
  // xwmhints.icon_pixmap = iconPixmap;
  xwmhints.initial_state = NormalState;
  xwmhints.flags = IconPixmapHint | StateHint;

  XSetWMHints(display, window, &xwmhints);
  XClearWindow(display, window);
  XSync(display, 0);
}

unsigned long calc_patch_crc(int ix, int iy) {
  int offset = (ix * CHUNKX) / PIXELS_PER_LONG +
               iy * CHUNKY * (CRTX_TOTAL / PIXELS_PER_LONG);
  unsigned long crc = 0x8154711;

  for (int x = 0; x < CHUNKX / PIXELS_PER_LONG; x++)
    for (int y = 0; y < CHUNKY; y++) {
      unsigned long dat;
      dat = crtbuf[offset + x + y * CRTX_TOTAL / PIXELS_PER_LONG];
      crc += (crc % 211 + dat);
    }
  return crc;
}

int repaint;
Pixmap pixmap;

void check_and_paint(int ix, int iy) {
  // unsigned long crc = calc_patch_crc(ix, iy);
  // if (!repaint && crc == crcs[ix][iy]) return;
  int offset = ix * CHUNKX + iy * CHUNKY * CRTX_TOTAL;

  XSetForeground(display, gc, 0x000000);
  XFillRectangle(display, pixmap, gc, 0, 0, CHUNKX, CHUNKY);
  for (int y = 0; y < CHUNKY; y++)
    for (int x = 0; x < CHUNKX; x += 1) {
      unsigned short data =
          (((unsigned short *)crtbuf)[offset + x + y * CRTX_TOTAL]);
      unsigned r = (data & 0xF800) >> 8;  // rrrrr... ........ -> rrrrr000
      unsigned g = (data & 0x07E0) >> 3;  // .....ggg ggg..... -> gggggg00
      unsigned b = (data & 0x1F) << 3;    // ............bbbbb -> bbbbb000
      unsigned long rgb = r * 65536 + g * 256 + b;
      XSetForeground(display, gc, rgb);
      XDrawPoint(display, pixmap, gc, x, y);
    }

  XCopyArea(display, pixmap, window, gc, 0, 0, CHUNKX, CHUNKY, ix * CHUNKX,
            iy * CHUNKY);
  // crcs[ix][iy] = crc;
}
#include <arpa/inet.h>
#include <sys/socket.h>
#define PORT 8118
#define MAXLINE 1000
void fbe_loop() {
  pixmap = XCreatePixmap(display, window, CHUNKX, CHUNKY, depth);
  XSelectInput(display, window,
               PointerMotionMask | ButtonPressMask | ButtonReleaseMask);
  int touch = 0;
  char message[12] = "1,1920,1080";
  int sockfd;
  struct sockaddr_in servaddr;
  // clear servaddr
  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
  servaddr.sin_port = htons(PORT);
  servaddr.sin_family = AF_INET;

  // create datagram socket
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  int x, y;
  bool send;
  // connect to server
  if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
    printf("\n Error : Connect Failed \n");
  }
  while (1) {
    repaint = 0;
    while ((XPending(display) > 0)) {
      XEvent event;
      XNextEvent(display, &event);
      send = false;
      switch (event.type) {
        case Expose:
          repaint = 1;
          break;
        case ButtonPress:
          touch = 1;
          send = true;
          break;
        case ButtonRelease:
          touch = 0;
          send = true;
          break;
        case MotionNotify:
          if (touch) send = true;
          break;
      }
      if (send) {
        XMotionEvent *e = (XMotionEvent *)&event;
        x = e->x < 0 ? 0 : e->x > CRTX ? CRTX : e->x;
        y = e->y < 0 ? 0 : e->y > CRTY ? CRTY : e->y;
        message[0] = '\0';
        sprintf(message, "%d,%d,%d", touch, x, y);
        sendto(sockfd, message, MAXLINE, 0, NULL, sizeof(servaddr));
      }
    }

    /*
       Sample all chunks for changes in shared memory buffer and
       eventually repaint individual chunks. Repaint everything if
       repaint is true (see above)
    */
    for (int y = 0; y < CRTY / CHUNKY; y++)
      for (int x = 0; x < CRTX / CHUNKX; x++) check_and_paint(x, y);
    usleep(16000);
  }
}

// void usr1_handler(int sig) { redocmap = 1; }

int main(int argc, char **argv) {
  int fd = open(FILE_FBE_BUFFER, O_RDONLY);
  if (fd > 0) {
    close(fd);
    // remove(FILE_FBE_BUFFER);
  } else {
    fd = open(FILE_FBE_BUFFER, O_CREAT | O_WRONLY, 0777);
    for (int i = 0; i < (CRTX_TOTAL * CRTY * BITS_PER_PIXEL / 8); i++)
      write(fd, "\000", 1);
    close(fd);
  }

  fd = open(FILE_FBE_BUFFER, O_RDWR);
  unsigned char *fbuf = mmap(NULL, (CRTX_TOTAL * CRTY * BITS_PER_PIXEL / 8),
                             PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  // signal(SIGUSR1, usr1_handler);
  fbe_init(fbuf);

  return 0;
}
