/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright (C) 2011 D. R. Commander.  All Rights Reserved.
 * Copyright 2017 Peter Astrand <astrand@cendio.se> for Cendio AB
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <locale.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <x0vncserver/Geometry.h>
#include <x0vncserver/PollingScheduler.h>
#include <x0vncserver/XDesktop.h>
#include <tx/TXWindow.h>

#include <X11/Xlib.h>
#include <X11/Xauth.h>
#include <X11/XKBlib.h>

#include <rfb/Logger_stdio.h>
#include <rfb/SecurityClient.h>
#include <rfb/SecurityServer.h>
#include <rfb/Security.h>
#ifdef HAVE_GNUTLS
#include <rfb/CSecurityTLS.h>
#endif
#include <rfb/LogWriter.h>
#include <rfb/Timer.h>
#include <rfb/Exception.h>
#include <rfb/VNCSConnectionST.h>
#include <network/UnixSocket.h>
#include <os/os.h>

#include <FL/Fl.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/fl_ask.H>
#include <FL/x.H>

#include "vncviewer/i18n.h"
#include "parameters.h"
#include "CConn.h"
#include "ServerDialog.h"
#include "UserDialog.h"
#include "x11clone.h"
#include "vncviewer/fltk_layout.h"

#undef PACKAGE_NAME
#define PACKAGE_NAME "x11clone"

rfb::LogWriter vlog("main");

using namespace network;
using namespace rfb;
using namespace std;

static Display* serverDpy = NULL;

char serverName[SERVERNAMELEN] = { '\0' };

static const char *argv0 = NULL;

static bool exitMainloop = false;
static const char *exitError = NULL;

XDesktop *desktop;
VNCServerST *server;
VNCSConnectionST* sconnection;
PollingScheduler *sched;

static const char *about_text()
{
  static char buffer[1024];

  // This is used in multiple places with potentially different
  // encodings, so we need to make sure we get a fresh string every
  // time.
  snprintf(buffer, sizeof(buffer),
           _("x11clone %d-bit v%s\n"
             "Built on: %s\n"
             "Copyright 2017 Peter Astrand for Cendio AB\n"
             "Copyright (C) 1999-%d TigerVNC Team and many others (see README.rst)\n"
             "See http://www.tigervnc.org for information on TigerVNC."),
           (int)sizeof(size_t)*8, PACKAGE_VERSION,
           BUILD_TIMESTAMP, 2017);

  return buffer;
}

void exit_x11clone(const char *error)
{
  // Prioritise the first error we get as that is probably the most
  // relevant one.
  if ((error != NULL) && (exitError == NULL))
    exitError = strdup(error);

  exitMainloop = true;
}

bool should_exit()
{
  return exitMainloop;
}

void about_x11clone()
{
  fl_message_title(_("About x11clone"));
  fl_message("%s", about_text());
}

void serverDpyEvent(FL_SOCKET fd, void *data)
{
  Display *dpy = (Display*)data;

  //fprintf(stderr, "serverDpyEvent\n");
  // Note that handleXEvents will not return until XPending returns
  // zero; until there are no more events to read from the
  // connection.
  TXWindow::handleXEvents(dpy);
}

void serverReadEvent(FL_SOCKET fd, void *data)
{
  VNCSConnectionST* sconnection = (VNCSConnectionST*)data;
  //fprintf(stderr, "serverReadEvent\n");
  sconnection->processMessages();
}

void serverWriteEvent(FL_SOCKET fd, void *data)
{
  VNCSConnectionST* sconnection = (VNCSConnectionST*)data;
  //fprintf(stderr, "serverWriteEvent\n");
  sconnection->flushSocket();
}


void flush_serverdpy()
{
  // The serverDpy fd is only selected for read, so flush anything in
  // the output buffer first
  XFlush(serverDpy);
}


void run_mainloop()
{
  int next_timer = 0;

  if (sconnection->getSock()->outStream().bufferUsage() > 0) {
    Fl::add_fd(sconnection->getSock()->getFd(), FL_WRITE, serverWriteEvent, sconnection);
  } else {
    Fl::remove_fd(sconnection->getSock()->getFd(), FL_WRITE);
  }
  //fprintf(stderr, "run_mainloop: server-buffer=%d\n",
  //	  sconnection->getSock()->outStream().bufferUsage());

  if (sched->isRunning()) {
    next_timer = sched->millisRemaining();
    if (next_timer > 500) {
      next_timer = 500;
    }
  }

  //fprintf(stderr, "next_timer:%d\n", next_timer);

  soonestTimeout(&next_timer, server->checkTimeouts());

  if (next_timer == 0)
    next_timer = INT_MAX;

  sched->sleepStarted();

  if (Fl::wait((double)next_timer / 1000.0) < 0.0) {
    vlog.error(_("Internal FLTK error. Exiting."));
    exit(-1);
  }

  // Some Xlib calls, for example XFlush, may enqueue events in the
  // input queue.
  if (XEventsQueued(serverDpy, QueuedAlready)) {
    TXWindow::handleXEvents(serverDpy);
  }

  sched->sleepFinished();

  if (desktop->isRunning() && sched->goodTimeToPoll()) {
    sched->newPass();
    desktop->poll();
  }
}


static void CleanupSignalHandler(int sig)
{
  // CleanupSignalHandler allows C++ object cleanup to happen because it calls
  // exit() rather than the default which is to abort.
  vlog.info(_("Termination signal %d has been received. x11clone will now exit."), sig);
  exit(1);
}

static void init_fltk()
{
  // Basic text size (10pt @ 96 dpi => 13px)
  FL_NORMAL_SIZE = 13;

  // Select a FLTK scheme and background color that looks somewhat
  // close to modern Linux and Windows.
  Fl::scheme("gtk+");
  Fl::background(220, 220, 220);

  // Proper Gnome Shell integration requires that we set a sensible
  // WM_CLASS for the window.
  Fl_Window::default_xclass("x11clone");

  // Set the default icon for all windows.
  const int icon_sizes[] = {48, 32, 24, 16};

  Fl_PNG_Image *icons[4];
  int count;

  count = 0;

  // FIXME: Follow icon theme specification
  for (size_t i = 0;i < sizeof(icon_sizes)/sizeof(icon_sizes[0]);i++) {
      char icon_path[PATH_MAX];
      bool exists;

      sprintf(icon_path, "%s/icons/hicolor/%dx%d/apps/x11clone.png",
              DATA_DIR, icon_sizes[i], icon_sizes[i]);

      struct stat st;
      if (stat(icon_path, &st) != 0)
        exists = false;
      else
        exists = true;

      if (exists) {
          icons[count] = new Fl_PNG_Image(icon_path);
          if (icons[count]->w() == 0 ||
              icons[count]->h() == 0 ||
              icons[count]->d() != 4) {
              delete icons[count];
              continue;
          }

          count++;
      }
  }

  Fl_Window::default_icons((const Fl_RGB_Image**)icons, count);

  for (int i = 0;i < count;i++)
      delete icons[i];

  // This makes the "icon" in dialogs rounded, which fits better
  // with the above schemes.
  fl_message_icon()->box(FL_UP_BOX);

  // Turn off the annoying behaviour where popups track the mouse.
  fl_message_hotspot(false);

  // Avoid empty titles for popups
  fl_message_title_default("x11clone");


  // FLTK exposes these so that we can translate them.
  fl_no     = _("No");
  fl_yes    = _("Yes");
  fl_ok     = _("OK");
  fl_cancel = _("Cancel");
  fl_close  = _("Close");

}


static void usage(const char *programName)
{
  fprintf(stderr,
          "\nusage: %s [parameters] [serverDisplay]\n"
          "       %s [parameters] xinitcmd [ [client] options ... ] [ -- [server] [serverDisplay] options ... ]",
          programName, programName);
  fprintf(stderr,"\n"
          "Parameters can be turned on with -<param> or off with -<param>=0\n"
          "Parameters which take a value can be specified as "
          "-<param> <value>\n"
          "Other valid forms are <param>=<value> -<param>=<value> "
          "--<param>=<value>\n"
          "Parameter names are case-insensitive.  The parameters are:\n\n");
  Configuration::listParams(79, 14);


  exit(1);
}

static void setRemoveParam(const char* param, const char* value)
{
    if (value) {
	Configuration::setParam(param, value);
    }
    Configuration::removeParam(param);
}

#define MAX_XINIT_ARGS 64

/* A preexec function must return zero, or the exec will be aborted */
typedef int (*preexec_ptr)(void *data);

pid_t
subprocess(char *const cmd[], preexec_ptr preexec_fn, void *preexec_data)
{
  int close_exec_pipe[2];

  if (pipe(close_exec_pipe) < 0) {
    vlog.error(_("pipe failed: %s"), strerror(errno));
    return -1;
  }

  if (fcntl(close_exec_pipe[1], F_SETFD, FD_CLOEXEC) < 0) {
    vlog.error(_("fcntl failed: %s"), strerror(errno));
    return -1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    vlog.error(_("fork failed: %s"), strerror(errno));
    return pid;
  }

  if (pid > 0) {
    /* parent, wait for exec */
    /* close write end of pipe */
    if (close(close_exec_pipe[1]) < 0) {
      vlog.error(_("close failed: %s"), strerror(errno));
    }
    /* block on read from close_exec_pipe */
    char onechar;
    ssize_t gotchars = read(close_exec_pipe[0], &onechar, 1);
    if (close(close_exec_pipe[0]) < 0) {
      vlog.error(_("close failed: %s"), strerror(errno));
    }
    if (gotchars != 0) {
      /* exec failed */
      return -1;
    }
    return pid;
  }

  /* child */
  /* close read end of pipe */
  if (close(close_exec_pipe[0]) < 0) {
    vlog.error(_("close failed: %s"), strerror(errno));
  }

  /* close all other fds */
  int fd = 3;
  int fdlimit = sysconf(_SC_OPEN_MAX);
  while (fd < fdlimit) {
    if (fd != close_exec_pipe[1]) {
      close(fd);
    }
    fd++;
  }

  /* execute preexec_fn */
  if (preexec_fn) {
    if ((*preexec_fn) (preexec_data) != 0) {
      goto fail;
    }
  }

  execvp(cmd[0], cmd);

  /* execvp failed */
  vlog.error(_("execvp failed: %s"), strerror(errno));

 fail:
  if (write(close_exec_pipe[1], "E", 1) < 0) {
    vlog.error(_("write failed: %s"), strerror(errno));
  }
  if (close(close_exec_pipe[1]) < 0) {
    vlog.error(_("close failed: %s"), strerror(errno));
  }
  close(2);
  _exit(71); /* system error (e.g., can't fork) */
  return 0;
}


static int
setup_xinit_fds(void *data)
{
  /* set stdin to /dev/null */
  if (close(0) < 0) {
    vlog.error(_("Unable to close stdin"));
  }
  int devnull = open("/dev/null", O_RDONLY);
  if (devnull < 0) {
    vlog.error(_("Unable to open /dev/null"));
  }
  else if (devnull != 0) {
    vlog.error(_("Didn't get fd 0 for stdin"));
  }

  /* close stdout */
  if (close(1) < 0) {
    vlog.error(_("Unable to close stdout"));
  }

  /* open log file */
  char *home = getenv("HOME");
  if (!home || chdir(home) < 0) {
    vlog.error(_("Unable to change to home dir: .xsession-errors created in working directory"));
  }
  int log = open(".xsession-errors", O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (log < 0) {
    vlog.error(_("Unable to open .xsession-errors for writing"));
  }
  else if (log != 1) {
    vlog.error(_("Didn't get fd 1 for stdout"));
  }

  /* since we are mixing stdout, stderr, and program output, it's
     convenient to have stdout at least line buffered */
  setvbuf(stdout, NULL, _IOLBF, 0);

  /* redirect stderr */
  if (close(2) < 0) {
    vlog.error(_(" unable to close stderr"));
  }
  else {
    dup(log);
  }

  return 0;
}

#define OPEN_DISP_RETRIES 40
/* Wait for Xserver to accept connections */
static int
waitforserver(pid_t pid)
{
  Display *disp = NULL;
  int status;

  for (int i = 0; i < OPEN_DISP_RETRIES; i++) {
    if ((disp = XOpenDisplay(serverName))) {
      return 0;
    }

    pid_t gotpid = waitpid(pid, &status, WNOHANG);
    if (gotpid < 0) {
      if (errno == ECHILD) {
	break;
      }
    }
    if (gotpid == pid) {
      break;
    }

    usleep(500000);
  }

  return -1;
}


/* Call XOpenDisplay, possibly without XAUTHORITY set */
static Display *OpenDisplayNoXauth(char *display_name)
{
  /* Xlib might print "No protocol specified" to stderr. Prefix this
     error message, so that we know where it comes from. See:
     https://bugs.freedesktop.org/show_bug.cgi?id=25722 */
  fprintf(stderr, "Xlib: ");
  Display *dpy = XOpenDisplay(display_name);
  if (!dpy) {
    char *xauthenv = getenv("XAUTHORITY");
    unsetenv("XAUTHORITY");
    dpy = XOpenDisplay(display_name);
    if (!dpy) {
      /* Didn't help, restore */
      setenv("XAUTHORITY", xauthenv, 1);
    }
  }
  /* Remove string above, in case nothing was printed */
  fprintf(stderr, "\b\b\b\b\b\b");
  return dpy;
}


int main(int argc, char** argv)
{
  UserDialog dlg;

  argv0 = argv[0];
  char *xinitargs[MAX_XINIT_ARGS] = { NULL };
  int xinitargc = 0;
  int displayarg = -1;

  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE_NAME, LOCALE_DIR);
  textdomain(PACKAGE_NAME);

  rfb::SecurityClient::secTypes.setParam("None");
  rfb::SecurityServer::secTypes.setParam("None");

  // Write about text to console, still using normal locale codeset
  fprintf(stderr,"\n%s\n", about_text());

  // Set gettext codeset to what our GUI toolkit uses. Since we are
  // passing strings from strerror/gai_strerror to the GUI, these must
  // be in GUI codeset as well.
  bind_textdomain_codeset(PACKAGE_NAME, "UTF-8");
  bind_textdomain_codeset("libc", "UTF-8");

  rfb::initStdIOLoggers();
  rfb::initFileLogger("/tmp/x11clone.log");
  logParams.setDefault("*:stderr:20,XDesktop:stderr:30,CConn:stderr:30");

#ifdef SIGHUP
  signal(SIGHUP, CleanupSignalHandler);
#endif
  signal(SIGINT, CleanupSignalHandler);
  signal(SIGTERM, CleanupSignalHandler);

  init_fltk();

  setRemoveParam("ZlibLevel", NULL);
  setRemoveParam("QueryConnect", NULL);
  setRemoveParam("AcceptSetDesktopSize", NULL);
  setRemoveParam("SendCutText", NULL);
  setRemoveParam("AcceptCutText", NULL);
  setRemoveParam("AcceptPointerEvents", NULL);
  setRemoveParam("AcceptKeyEvents", NULL);
  setRemoveParam("DisconnectClients", NULL);
  setRemoveParam("NeverShared", NULL);
  setRemoveParam("AlwaysShared", NULL);
  setRemoveParam("Protocol3.3", NULL);
  setRemoveParam("compareFB", "0");
  setRemoveParam("ClientWaitTimeMillis", NULL);
  setRemoveParam("MaxConnectionTime", NULL);
  setRemoveParam("MaxDisconnectionTime", NULL);
  setRemoveParam("IdleTimeout", NULL);
  setRemoveParam("ImprovedHextile", NULL);
  setRemoveParam("BlacklistTimeout", NULL);
  setRemoveParam("BlacklistThreshold", NULL);
  setRemoveParam("PlainUsers", NULL);
  setRemoveParam("PointerEventInterval", NULL);
  setRemoveParam("pam_service", NULL);
  setRemoveParam("PAMService", NULL);
  setRemoveParam("GnuTLSPriority", NULL);

  // New name and description for server side "Geometry"
  if (VoidParameter *p = Configuration::getParam("Geometry")) {
      setRemoveParam("Geometry", NULL);
      new AliasParameter("ServerGeometry",
			 "Server display screen area to show. "
			 "Format is <width>x<height>+<offset_x>+<offset_y>, "
			 "more information in man X, section GEOMETRY SPECIFICATIONS. "
			 "If the argument is empty, the entire screen is shown.",
			 p);
  }

  for (int i = 1; i < argc;) {
    if (Configuration::setParam(argv[i])) {
      i++;
      continue;
    }

    if (argv[i][0] == '-') {
      if (i+1 < argc) {
        if (Configuration::setParam(&argv[i][1], argv[i+1])) {
          i += 2;
          continue;
        }
      }

      usage(argv[0]);
    }

    if (argc-i == 1) {
      strncpy(serverName, argv[i], SERVERNAMELEN);
      serverName[SERVERNAMELEN - 1] = '\0';
    } else if (argc-i > MAX_XINIT_ARGS-1) {
      vlog.error(_("Too many arguments; only %d allowed"), MAX_XINIT_ARGS-1);
      return 1;
    } else {
      strcpy(serverName, ":0"); // Default to :0, just like xinit does
      for (int j = i; j < argc; j++) {
	xinitargs[xinitargc++] = argv[j];
	if (!strcmp("--", argv[j])) {
	  /* syntax requires that display is directly after server */
	  displayarg = j + 2;
        }
        if (j == displayarg) {
	  /* retrieve display number */
	  strncpy(serverName, argv[j], SERVERNAMELEN);
	  serverName[SERVERNAMELEN - 1] = '\0';
        }
      }
      xinitargs[xinitargc++] = NULL;
    }
    break;
  }

  if (strcmp(display, "") != 0) {
    Fl::display(display);
  }
  fl_open_display();
  XkbSetDetectableAutoRepeat(fl_display, True, NULL);

  if (xinitargc > 0) {
    /* xinit command given */
    /* Check if server is already running - try connecting */
    serverDpy = OpenDisplayNoXauth(serverName);
    if (serverDpy) {
      /* Close and let the code below open */
      XCloseDisplay(serverDpy);
    } else {
      vlog.status(_("Cannot connect to display - starting Xserver (output to ~/.xsession-errors)"));
      /* Check if XAUTHORITY is writable, otherwise clear */
      if (getenv("XAUTHORITY") && access(XauFileName(), W_OK) < 0) {
	vlog.status(_("Cannot write to XAUTHORITY - unsetting"));
	unsetenv("XAUTHORITY");
      }
      pid_t xinit = subprocess(xinitargs, setup_xinit_fds, NULL);
      if (xinit < 0) {
	vlog.error(_("Unable to start Xserver"));
	if (alertOnFatalError) {
	  fl_alert(_("Unable to start Xserver"));
	}
      } else {
	if (waitforserver(xinit) < 0) {
	  vlog.error(_("Xserver failed to start within timeout"));
	  if (alertOnFatalError) {
	    fl_alert(_("Xserver failed to start within timeout"));
	  }
	} else {
	  vlog.status(_("Xserver started"));
	}
      }
    }
  }

  bool cmdLineServer = false;
  /* RFB server - connect to server display */
  if (serverName[0] == '\0') {
    strcpy(serverName, ":0");
  } else {
    // Server name given on command line
    cmdLineServer = true;
  }

  do {
    if (!cmdLineServer) {
      ServerDialog::run(serverName, serverName);
      if (serverName[0] == '\0') {
	return 1;
      }
    }
    serverDpy = OpenDisplayNoXauth(serverName);

    if (check) {
      return !serverDpy;
    }

    if (!serverDpy) {
      vlog.error(_("Unable to open display \"%s\""), serverName);
      if (!cmdLineServer || alertOnFatalError) {
	fl_alert(_("Unable to open display \"%s\""), serverName);
      }
      if (cmdLineServer) {
	return 1;
      }
    }
  } while (!serverDpy);

  TXWindow::init(serverDpy, "x11clone");
  Geometry geo(DisplayWidth(serverDpy, DefaultScreen(serverDpy)),
	       DisplayHeight(serverDpy, DefaultScreen(serverDpy)));
  if (geo.getRect().is_empty()) {
    vlog.error(_("Invalid server geometry"));
    if (alertOnFatalError) {
      fl_alert(_("Invalid server geometry"));
    }
    return 1;
  }
  try {
    desktop = new XDesktop(serverDpy, &geo);
    server = new VNCServerST(serverName, desktop);
  } catch (rdr::Exception &e) {
    vlog.error("%s", e.str());
    if (alertOnFatalError) {
      fl_alert("%s", e.str());
    }
    return 1;
  }

  int pairfds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pairfds) < 0) {
    vlog.error(_("socketpair failed: %s"), strerror(errno));
    if (alertOnFatalError) {
      fl_alert(_("socketpair failed: %s"), strerror(errno));
    }
    return 1;
  }

  UnixSocket serversocket(pairfds[1], true, 33177600);
  serversocket.outStream().setBlocking(false);
  server->addSocket(&serversocket);

  sconnection = (VNCSConnectionST*)server->getSConnection(&serversocket);

  Fl::add_fd(ConnectionNumber(serverDpy), FL_READ, serverDpyEvent, serverDpy);
  Fl::add_fd(serversocket.getFd(), FL_READ, serverReadEvent, sconnection);
  TXWindow::handleXEvents(serverDpy);

  //fprintf(stderr, "FDs: serverDpy=%d, server=%d, client=%d\n",
  //ConnectionNumber(serverDpy), pairfds[1], pairfds[0]);

  sched = new PollingScheduler((int)pollingCycle, (int)maxProcessorUsage);

  /* RFB client */
  CSecurity::upg = &dlg;
#ifdef HAVE_GNUTLS
  CSecurityTLS::msg = &dlg;
#endif

  UnixSocket *clientsocket = new UnixSocket(pairfds[0]);
  CConn *cc = new CConn(clientsocket);
  clientsocket->outStream().setBlocking(false);

  while (!exitMainloop)
    run_mainloop();

  delete cc;

  delete sched;
  delete server;
  delete desktop;

  if (exitError != NULL && alertOnFatalError)
    fl_alert("%s", exitError);

  return 0;
}
