/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright (C) 2011 D. R. Commander.  All Rights Reserved.
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

#include <x0vncserver/Geometry.h>
#include <x0vncserver/PollingScheduler.h>
#include <x0vncserver/XDesktop.h>
#include <tx/TXWindow.h>

#ifdef WIN32
#include <os/winerrno.h>
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

#if !defined(WIN32) && !defined(__APPLE__)
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#endif

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

#include "i18n.h"
#include "parameters.h"
#include "CConn.h"
#include "ServerDialog.h"
#include "UserDialog.h"
#include "x11clone.h"
#include "fltk_layout.h"

#ifdef WIN32
#include "resource.h"
#include "win32.h"
#endif

rfb::LogWriter vlog("main");

using namespace network;
using namespace rfb;
using namespace std;

static Display* cloneDpy = NULL;

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

void cloneDpyEvent(FL_SOCKET fd, void *data)
{
  Display *dpy = (Display*)data;

  //fprintf(stderr, "cloneDpyEvent\n");
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

void run_mainloop()
{
  // The cloneDpy fd is only selected for read, so flush anything in
  // the output buffer first
  XFlush(cloneDpy);

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

  sched->sleepFinished();

  if (desktop->isRunning() && sched->goodTimeToPoll()) {
    sched->newPass();
    desktop->poll();
  }
}

#ifdef __APPLE__
static void about_callback(Fl_Widget *widget, void *data)
{
  about_x11clone();
}

static void new_connection_cb(Fl_Widget *widget, void *data)
{
  const char *argv[2];
  pid_t pid;

  pid = fork();
  if (pid == -1) {
    vlog.error(_("Error starting new x11clone: %s"), strerror(errno));
    return;
  }

  if (pid != 0)
    return;

  argv[0] = argv0;
  argv[1] = NULL;

  execvp(argv[0], (char * const *)argv);

  vlog.error(_("Error starting new x11clone: %s"), strerror(errno));
  _exit(1);
}
#endif

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

#ifndef __APPLE__
  // Select a FLTK scheme and background color that looks somewhat
  // close to modern Linux and Windows.
  Fl::scheme("gtk+");
  Fl::background(220, 220, 220);
#else
  // On Mac OS X there is another scheme that fits better though.
  Fl::scheme("plastic");
#endif

  // Proper Gnome Shell integration requires that we set a sensible
  // WM_CLASS for the window.
  Fl_Window::default_xclass("x11clone");

  // Set the default icon for all windows.
#ifdef WIN32
  HICON lg, sm;

  lg = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON),
                        IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                        GetSystemMetrics(SM_CYICON),
                        LR_DEFAULTCOLOR | LR_SHARED);
  sm = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON),
                        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                        GetSystemMetrics(SM_CYSMICON),
                        LR_DEFAULTCOLOR | LR_SHARED);

  Fl_Window::default_icons(lg, sm);
#elif ! defined(__APPLE__)
  const int icon_sizes[] = {48, 32, 24, 16};

  Fl_PNG_Image *icons[4];
  int count;

  count = 0;

  // FIXME: Follow icon theme specification
  for (size_t i = 0;i < sizeof(icon_sizes)/sizeof(icon_sizes[0]);i++) {
      char icon_path[PATH_MAX];
      bool exists;

      sprintf(icon_path, "%s/icons/hicolor/%dx%d/apps/tigervnc.png",
              DATA_DIR, icon_sizes[i], icon_sizes[i]);

#ifndef WIN32
      struct stat st;
      if (stat(icon_path, &st) != 0)
#else
      struct _stat st;
      if (_stat(icon_path, &st) != 0)
          return(false);
#endif
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
#endif

  // This makes the "icon" in dialogs rounded, which fits better
  // with the above schemes.
  fl_message_icon()->box(FL_UP_BOX);

  // Turn off the annoying behaviour where popups track the mouse.
  fl_message_hotspot(false);

  // Avoid empty titles for popups
  fl_message_title_default(_("x11clone"));

#ifdef WIN32
  // Most "normal" Windows apps use this font for UI elements.
  Fl::set_font(FL_HELVETICA, "Tahoma");
#endif

  // FLTK exposes these so that we can translate them.
  fl_no     = _("No");
  fl_yes    = _("Yes");
  fl_ok     = _("OK");
  fl_cancel = _("Cancel");
  fl_close  = _("Close");

#ifdef __APPLE__
  /* Needs trailing space */
  static char fltk_about[16];
  snprintf(fltk_about, sizeof(fltk_about), "%s ", _("About"));
  Fl_Mac_App_Menu::about = fltk_about;
  static char fltk_hide[16];
  snprintf(fltk_hide, sizeof(fltk_hide), "%s ", _("Hide"));
  Fl_Mac_App_Menu::hide = fltk_hide;
  static char fltk_quit[16];
  snprintf(fltk_quit, sizeof(fltk_quit), "%s ", _("Quit"));
  Fl_Mac_App_Menu::quit = fltk_quit;

  Fl_Mac_App_Menu::print = ""; // Don't want the print item
  Fl_Mac_App_Menu::services = _("Services");
  Fl_Mac_App_Menu::hide_others = _("Hide Others");
  Fl_Mac_App_Menu::show = _("Show All");

  fl_mac_set_about(about_callback, NULL);

  Fl_Sys_Menu_Bar *menubar;
  char buffer[1024];
  menubar = new Fl_Sys_Menu_Bar(0, 0, 500, 25);
  // Fl_Sys_Menu_Bar overrides methods without them being virtual,
  // which means we cannot use our generic Fl_Menu_ helpers.
  if (fltk_menu_escape(p_("SysMenu|", "&File"),
                       buffer, sizeof(buffer)) < sizeof(buffer))
      menubar->add(buffer, 0, 0, 0, FL_SUBMENU);
  if (fltk_menu_escape(p_("SysMenu|File|", "&New Connection"),
                       buffer, sizeof(buffer)) < sizeof(buffer))
      menubar->insert(1, buffer, FL_COMMAND | 'n', new_connection_cb);
#endif
}


static void usage(const char *programName)
{
#ifdef WIN32
  // If we don't have a console then we need to create one for output
  if (GetConsoleWindow() == NULL) {
    HANDLE handle;
    int fd;

    AllocConsole();

    handle = GetStdHandle(STD_ERROR_HANDLE);
    fd = _open_osfhandle((intptr_t)handle, O_TEXT);
    *stderr = *fdopen(fd, "w");
  }
#endif

  fprintf(stderr,
          "\nusage: %s [parameters] [host:displayNum] [parameters]\n",
          programName);
  fprintf(stderr,"\n"
          "Parameters can be turned on with -<param> or off with -<param>=0\n"
          "Parameters which take a value can be specified as "
          "-<param> <value>\n"
          "Other valid forms are <param>=<value> -<param>=<value> "
          "--<param>=<value>\n"
          "Parameter names are case-insensitive.  The parameters are:\n\n");
  Configuration::listParams(79, 14);

#ifdef WIN32
  // Just wait for the user to kill the console window
  Sleep(INFINITE);
#endif

  exit(1);
}

int main(int argc, char** argv)
{
  UserDialog dlg;

  argv0 = argv[0];

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
#ifdef WIN32
  rfb::initFileLogger("C:\\temp\\x11clone.log");
#else
  rfb::initFileLogger("/tmp/x11clone.log");
#endif
  rfb::LogWriter::setLogParams("*:stderr:30");

#ifdef SIGHUP
  signal(SIGHUP, CleanupSignalHandler);
#endif
  signal(SIGINT, CleanupSignalHandler);
  signal(SIGTERM, CleanupSignalHandler);

  init_fltk();

#if !defined(WIN32) && !defined(__APPLE__)
  fl_open_display();
  XkbSetDetectableAutoRepeat(fl_display, True, NULL);
#endif

  int i = 1;
  if (!Fl::args(argc, argv, i) || i < argc)
    for (; i < argc; i++) {
      if (Configuration::setParam(argv[i]))
        continue;

      if (argv[i][0] == '-') {
        if (i+1 < argc) {
          if (Configuration::setParam(&argv[i][1], argv[i+1])) {
            i++;
            continue;
          }
        }
        usage(argv[0]);
      }

    }

  const char *s = cloneDisplay.getData();
  strncpy(serverName, s, SERVERNAMELEN);
  serverName[SERVERNAMELEN - 1] = '\0';
  delete s;

  if (serverName[0] == '\0') {
    ServerDialog::run(":0", serverName);
    if (serverName[0] == '\0')
      return 1;
  }

  /* RFB server - connect to cloned display */
  if (!(cloneDpy = XOpenDisplay(serverName))) {
    // FIXME: Why not vlog.error(...)?
    fprintf(stderr,"%s: unable to open display \"%s\"\r\n",
	    "x11clone", serverName);
    return 1;
  }
  TXWindow::init(cloneDpy, "x11clone");
  Geometry geo(DisplayWidth(cloneDpy, DefaultScreen(cloneDpy)),
	       DisplayHeight(cloneDpy, DefaultScreen(cloneDpy)));
  if (geo.getRect().is_empty()) {
    vlog.error("Exiting with error");
    return 1;
  }
  try {
    desktop = new XDesktop(cloneDpy, &geo);
    server = new VNCServerST(serverName, desktop);
  } catch (rdr::Exception &e) {
    vlog.error("%s", e.str());
    return 1;
  }

  int pairfds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, pairfds) < 0) {
    vlog.error(_("socketpair failed: %s"), strerror(errno));
    return 1;
  }

  UnixSocket serversocket(pairfds[1], true, 33177600);
  serversocket.outStream().setBlocking(false);
  server->addSocket(&serversocket);

  sconnection = (VNCSConnectionST*)server->getSConnection(&serversocket);

  Fl::add_fd(ConnectionNumber(cloneDpy), FL_READ, cloneDpyEvent, cloneDpy);
  Fl::add_fd(serversocket.getFd(), FL_READ, serverReadEvent, sconnection);
  TXWindow::handleXEvents(cloneDpy);

  //fprintf(stderr, "FDs: cloneDpy=%d, server=%d, client=%d\n",
  //ConnectionNumber(cloneDpy), pairfds[1], pairfds[0]);

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
