/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright (C) 2011 D. R. Commander.  All Rights Reserved.
 * Copyright 2018 Peter Astrand <astrand@cendio.se> for Cendio AB
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
#include <rfb/Security.h>
#include <rfb/LogWriter.h>
#include <rfb/Timer.h>
#include <rfb/Exception.h>
#ifndef WIN32
#include <network/UnixSocket.h>
#include <sys/socket.h> // MSG_PEEK, recv
#endif
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
#include "vncviewer.h"
#include "fltk_layout.h"

#ifdef WIN32
#include "resource.h"
#include "win32.h"
#endif

rfb::LogWriter vlog("main");
rfb::LogWriter vlogserver("Server");

using namespace network;
using namespace rfb;
using namespace std;

char serverName[VNCSERVERNAMELEN] = { '\0' }; // "server display"

// Local x0vncserver process, or over SSH
pid_t server_pid = 0;

static const char *argv0 = NULL;

static bool exitMainloop = false;
static const char *exitError = NULL;

static const char *about_text()
{
  static char buffer[1024];

  // This is used in multiple places with potentially different
  // encodings, so we need to make sure we get a fresh string every
  // time.
  snprintf(buffer, sizeof(buffer),
           _("x11clone %d-bit v%s\n"
             "Built on: %s\n"
             "Copyright 2018 Peter Astrand for Cendio AB\n"
             "Copyright (C) 1999-%d TigerVNC Team and many others (see README.rst)\n"
             "See http://www.tigervnc.org for information on TigerVNC."),
           (int)sizeof(size_t)*8, PACKAGE_VERSION,
           BUILD_TIMESTAMP, 2018);

  return buffer;
}

void exit_vncviewer(const char *error)
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

void about_vncviewer()
{
  fl_message_title(_("About x11clone"));
  fl_message("%s", about_text());
}

void run_mainloop()
{
  int next_timer;

  next_timer = Timer::checkTimeouts();
  if (next_timer == 0)
    next_timer = INT_MAX;

  if (Fl::wait((double)next_timer / 1000.0) < 0.0 && !exitMainloop) {
    vlog.error(_("Internal FLTK error. Exiting."));
    exit(-1);
  }
}

#ifdef __APPLE__
static void about_callback(Fl_Widget *widget, void *data)
{
  about_vncviewer();
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
  vlog.info(_("Termination signal %d has been received. x11clone will now exit."), sig);
  exitMainloop = true;
}

static void init_fltk()
{
  // Basic text size (10pt @ 96 dpi => 13px)
  FL_NORMAL_SIZE = 13;

  // Select a FLTK scheme and background color that looks somewhat
  // close to modern systems
  Fl::scheme("gtk+");
  Fl::background(220, 220, 220);

  // macOS has a slightly brighter default background though
#ifdef __APPLE__
  Fl::background(240, 240, 240);
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

static void mkvnchomedir()
{
  // Create .vnc in the user's home directory if it doesn't already exist
  char* homeDir = NULL;

  if (getvnchomedir(&homeDir) == -1) {
    vlog.error(_("Could not create VNC home directory: can't obtain home "
                 "directory path."));
  } else {
    int result = mkdir(homeDir, 0755);
    if (result == -1 && errno != EEXIST)
      vlog.error(_("Could not create VNC home directory: %s."), strerror(errno));
    delete [] homeDir;
  }
}

static void setRemoveParam(const char* param, const char* value)
{
    if (value) {
	Configuration::setParam(param, value);
    }
    Configuration::removeParam(param);
}

#ifndef WIN32
static Socket *connect_to_socket(const char *localUnixSocket)
{
  Socket *sock = NULL;

  // It might take some time until SSH has created the local socket
  // and for x0vncserver to start accepting connections on the remote
  // socket, so loop
  int retries = 20;
  while (retries) {
    try {
      if (--retries == 0 || exitMainloop) {
        throw SocketException("Unable to connect to server", ECONNREFUSED);
      }
    } catch (rdr::Exception& e) {
      vlog.error("%s", e.str());
      if (alertOnFatalError)
        fl_alert("%s", e.str());
      exit_vncviewer();
      exit(1);
    }

    try {
      sock = new network::UnixSocket(localUnixSocket);
    } catch (rdr::Exception& e) {
      usleep(500000);
      continue;
    }

    // Now try read
    char b;
    if (recv(sock->getFd(), &b, 1, MSG_PEEK) > 0) {
      break;
    }

    delete sock;
    sock = NULL;
    usleep(500000);
  }

  return sock;
}

/* A preexec function must return zero, or the exec will be aborted */
typedef int (*preexec_ptr)(void *data);

pid_t
static subprocess(char *const cmd[], preexec_ptr preexec_fn, void *preexec_data)
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

  /* execute preexec_fn */
  if (preexec_fn) {
    if ((*preexec_fn) (preexec_data) != 0) {
      goto fail;
    }
  }

  /* close all other fds */
  {
    int fd = 3;
    int fdlimit = sysconf(_SC_OPEN_MAX);
    while (fd < fdlimit) {
      if (fd != close_exec_pipe[1]) {
	close(fd);
      }
      fd++;
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

static void serverEvent(FL_SOCKET fd, void*data)
{
  static char buf[1024];
  static char *p = NULL; /* Pointer to free space after a partial data */

  if (!p) {
    p = buf;
  }

  /* Room för trailing zero */
  size_t room = buf + sizeof(buf) - p - 1;
  /* If buffer was filled without any newline, just print the data */
  if (room == 0) {
    vlogserver.info("%s", buf);
    p = buf;
    room = sizeof(buf) - 1;
  }

  /* Read */
  ssize_t nrbytes = read(fd, p, room);
  p[nrbytes] = '\0';

  /* Eliminate \r */
  char *q = buf;
  char *cr;
  while ((cr = strchr(q, '\r'))) {
    *cr = '\n';
    q = cr;
    q++;
  }

  /* Print with prefix */
  q = buf;
  char *nl;
  while ((nl = strchr(q, '\n'))) {
    *nl = '\0';
    if (*q) {
      vlogserver.info("%s", q);
    }
    q = nl;
    q++;
  }

  /* q now points to the next chunk of data without newline,
     or zero */
  p = buf + strlen(q);
  memmove(buf, q, p - buf + 1);
}

static int
setup_server_process(void *data)
{
  int (*datapipe)[2] = (int(*)[2])data;

  /* close unused read end */
  if (close((*datapipe)[0]) < 0) {
    vlog.error(_("child: unable to close read end of pipe: %s"), strerror(errno));
    /* not fatal */
  }

  /* /dev/null as stdin */
  int nullfd;
  if ((nullfd = open("/dev/null", O_RDONLY)) < 0) {
    vlog.error(_("child: unable to open /dev/null: %s"), strerror(errno));
    return -1;
  }
  if (close(0) < 0) {
    vlog.error(_("child: unable to close stdin: %s"), strerror(errno));
    return -1;
  }
  if (dup(nullfd) < 0) {
    vlog.error(_("child: unable to setup stdin: %s"), strerror(errno));
    return -1;
  }

  /* make stdout and stderr */
  if (close(1) < 0) {
    vlog.error(_("child: unable to close stdout: %s"), strerror(errno));
    return -1;
  }
  if (dup((*datapipe)[1]) < 0) {
    vlog.error(_("child: unable to setup stdout: %s"), strerror(errno));
    return -1;
  }
  if (close(2) < 0) {
    vlog.error(_("child: unable to close stderr: %s"), strerror(errno));
    return -1;
  }
  if (dup((*datapipe)[1]) < 0) {
    vlog.error(_("child: unable to setup stderr: %s"), strerror(errno));
    return -1;
  }

  /* disconnect from controlling terminal, so that Ctrl-C etc does not
     go directly to this process */
  setsid();

  return 0;
}

static int startServer(const char *localUnixSocket)
{
  const char *x0cmd = "x0vncserver -display=\"$D\" -rfbunixpath=\"$R\" -SecurityTypes=None -RawKeyboard=1";
  char servercmd[4096] = "";

  if (strlen (via.getValueStr()) > 0) {
    /* Start x0vncserver on remote host over SSH */

    const char *gatewayHost;
    gatewayHost = strDup(via.getValueStr());

    // x0vncserver unlinks an existing socket before use, but SSH
    // does not
    if (unlink(localUnixSocket)) {
      vlog.error(_("Unable to remove temporary file: %s."), strerror(errno));
      return 1;
    }

    // Determine a name for the remote Unix socket. Unfortunately, since
    // we are executing on the remote host, we have to guess on
    // something unique.
    char remoteUnixSocket[sizeof("/tmp/x11clone_remote_") + SERVERNAMELEN + 32];
    struct timeval now;
    gettimeofday(&now, NULL);
    snprintf(remoteUnixSocket, sizeof(remoteUnixSocket),
	     "/tmp/x11clone_remote_%s_%lx.%lx",
	     serverName, now.tv_sec, now.tv_usec);
    // Must remove any colons, or SSH will think this is a TCP port forward
    char *colon;
    while ((colon = strchr(remoteUnixSocket, ':')) != NULL)
      *colon = '_';

    const char *viacmd = getenv("X11CLONE_VIA_CMD");
    if (!viacmd)
      viacmd = "ssh -t -t -L \"$L\":\"$R\" \"$G\"";

    strncpy(servercmd, viacmd, sizeof(servercmd));
    servercmd[sizeof(servercmd)-1] = '\0';

    setenv("G", gatewayHost, 1);
    setenv("R", remoteUnixSocket, 1);
    setenv("L", localUnixSocket, 1);
  } else {
    setenv("R", localUnixSocket, 1);
  }

  setenv("D", serverName, 1);

  strncat(servercmd, " ", sizeof(servercmd) - strlen(servercmd) - 1);
  strncat(servercmd, x0cmd, sizeof(servercmd) - strlen(servercmd) - 1);
  servercmd[sizeof(servercmd)-1] = '\0';

  // Using subprocess() instead of system() etc allows us to terminate
  // SSH at exit, and more. Since we are using -t to SSH, a SIGINT to
  // SSH will be forwarded to x0vncserver.
  char *cmdargs[8] = { NULL };
  int cmdargc = 0;
  cmdargs[cmdargc++] = (char*)"/bin/sh";
  cmdargs[cmdargc++] = (char*)"-c";
  cmdargs[cmdargc++] = servercmd;
  cmdargs[cmdargc++] = NULL;

  // Create pipe for server output
  int datapipe[2];
  if (pipe(datapipe) < 0) {
    vlog.error(_("Cannot create pipe to subprocess: %s"), strerror(errno));
    return 0;
  }

  server_pid = subprocess(cmdargs, setup_server_process, datapipe);
  Fl::add_fd(datapipe[0], FL_READ | FL_EXCEPT, serverEvent, NULL);

  return 0;
}
#endif

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
          "\nusage: %s [parameters] [serverDisplay] [parameters]\n"
          "       %s [parameters] [.tigervnc file]\n",
          programName, programName);
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

static void
potentiallyLoadConfigurationFile(char *serverName)
{
  const bool hasPathSeparator = (strchr(serverName, '/') != NULL ||
                                 (strchr(serverName, '\\')) != NULL);

  if (hasPathSeparator) {
#ifndef WIN32
    struct stat sb;

    // This might be a UNIX socket, we need to check
    if (stat(serverName, &sb) == -1) {
      // Some access problem; let loadViewerParameters() deal with it...
    } else {
      if ((sb.st_mode & S_IFMT) == S_IFSOCK)
        return;
    }
#endif

    try {
      const char* newServerName;
      newServerName = loadViewerParameters(serverName);
      // This might be empty, but we still need to clear it so we
      // don't try to connect to the filename
      strncpy(serverName, newServerName, VNCSERVERNAMELEN);
    } catch (rfb::Exception& e) {
      vlog.error("%s", e.str());
      if (alertOnFatalError)
        fl_alert("%s", e.str());
      exit(EXIT_FAILURE);
    }
  }
}

int main(int argc, char** argv)
{
  UserDialog dlg;

  argv0 = argv[0];

  setlocale(LC_ALL, "");
  bindtextdomain(PACKAGE_NAME, LOCALE_DIR);
  textdomain(PACKAGE_NAME);

  rfb::SecurityClient::setDefaults();

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

  setRemoveParam("UseIPv6", NULL);
  setRemoveParam("UseIPv4", NULL);
  setRemoveParam("Shared", NULL);
  setRemoveParam("PasswordFile", NULL);
  setRemoveParam("passwd", NULL);
  setRemoveParam("listen", NULL);
  // Change default for RemoteResize
  Configuration::setParam("RemoteResize", "0");

  /* Load the default parameter settings */
  char defaultServerName[VNCSERVERNAMELEN] = "";
  try {
    const char* configServerName;
    configServerName = loadViewerParameters(NULL);
    if (configServerName != NULL)
      strncpy(defaultServerName, configServerName, VNCSERVERNAMELEN);
  } catch (rfb::Exception& e) {
    vlog.error("%s", e.str());
    if (alertOnFatalError)
      fl_alert("%s", e.str());
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

    strncpy(serverName, argv[i], VNCSERVERNAMELEN);
    serverName[VNCSERVERNAMELEN - 1] = '\0';
    i++;
  }

  // Check if the server name in reality is a configuration file
  potentiallyLoadConfigurationFile(serverName);

  mkvnchomedir();

#if !defined(WIN32) && !defined(__APPLE__)
  if (strcmp(display, "") != 0) {
    Fl::display(display);
  }
  fl_open_display();
  XkbSetDetectableAutoRepeat(fl_display, True, NULL);
#endif

  CSecurity::upg = &dlg;

  Socket *sock = NULL;

  if (serverName[0] == '\0') {
    ServerDialog::run(defaultServerName, serverName);
    if (serverName[0] == '\0')
      return 1;
  }

  // Determine a name for the local UNIX socket; to be used either by
  // x0vncserver running locally, or ssh.
  char localUnixSocket[] = "/tmp/x11clone_XXXXXX";
  int fd = mkstemp(localUnixSocket);
  if (fd < 0) {
    vlog.error(_("Could not create temporary file: %s."), strerror(errno));
    return 1;
  }
  if (close(fd) < 0) {
    vlog.error(_("Unable to close temporary file: %s."), strerror(errno));
    return 1;
  }

#ifndef WIN32
  startServer(localUnixSocket);
#endif

  sock = connect_to_socket(localUnixSocket);
  CConn *cc = new CConn("", sock);

  while (!exitMainloop)
    run_mainloop();

  delete cc;

  // Stay around a little bit longer in order to catch and print
  // server messages
  while(Fl::wait(1));

  // Terminate SSH and x0vncserver
  kill(server_pid, SIGINT);

  // Delete UNIX socket (created by ssh)
  if (unlink(localUnixSocket)) {
    vlog.error(_("Unable to remove temporary file: %s."), strerror(errno));
    return 1;
  }

  if (exitError != NULL && alertOnFatalError)
    fl_alert("%s", exitError);

  return 0;
}
