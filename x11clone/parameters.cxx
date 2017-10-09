/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright 2012 Samuel Mannehed <samuel@cendio.se> for Cendio AB
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

#ifdef HAVE_GNUTLS
#include <rfb/CSecurityTLS.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <tchar.h>
#endif

#include "parameters.h"

#include <os/os.h>
#include <rfb/Exception.h>
#include <rfb/LogWriter.h>
#include <rfb/SecurityClient.h>

#include <FL/fl_utf8.h>

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include "i18n.h"

using namespace rfb;

static LogWriter vlog("Parameters");


IntParameter pointerEventInterval("PointerEventInterval",
                                  "Time in milliseconds to rate-limit"
                                  " successive pointer events", 17);
BoolParameter dotWhenNoCursor("DotWhenNoCursor",
                              "Show the dot cursor when the server sends an "
                              "invisible cursor", false);

BoolParameter alertOnFatalError("AlertOnFatalError",
                                "Give a dialog on connection problems rather "
                                "than exiting immediately", true);

StringParameter passwordFile("PasswordFile",
                             "Password file for VNC authentication", "");
AliasParameter passwd("passwd", "Alias for PasswordFile", &passwordFile);

BoolParameter fullColour("FullColor",
                         "Use full color", true);
AliasParameter fullColourAlias("FullColour", "Alias for FullColor", &fullColour);
IntParameter lowColourLevel("LowColorLevel",
                            "Color level to use on slow connections. "
                            "0 = Very Low (8 colors), 1 = Low (64 colors), "
                            "2 = Medium (256 colors)", 2);
AliasParameter lowColourLevelAlias("LowColourLevel", "Alias for LowColorLevel", &lowColourLevel);
StringParameter preferredEncoding("PreferredEncoding",
                                  "Preferred encoding to use (Tight, ZRLE, Hextile or"
                                  " Raw)", "Tight");
BoolParameter customCompressLevel("CustomCompressLevel",
                                  "Use custom compression level. "
                                  "Default if CompressLevel is specified.", false);
IntParameter compressLevel("CompressLevel",
                           "Use specified compression level 0 = Low, 6 = High",
                           2);
BoolParameter noJpeg("NoJPEG",
                     "Disable lossy JPEG compression in Tight encoding.",
                     false);
IntParameter qualityLevel("QualityLevel",
                          "JPEG quality level. 0 = Low, 9 = High",
                          8);

BoolParameter maximize("Maximize", "Maximize viewer window", false);
BoolParameter fullScreen("FullScreen", "Full screen mode", false);
BoolParameter fullScreenAllMonitors("FullScreenAllMonitors",
                                    "Enable full screen over all monitors",
                                    true);
StringParameter desktopSize("DesktopSize",
                            "Reconfigure desktop size on the server on "
                            "connect (if possible)", "");
StringParameter geometry("geometry",
                         "Specify size and position of viewer window", "");

BoolParameter listenMode("listen", "Listen for connections from VNC servers", false);

BoolParameter remoteResize("RemoteResize",
                           "Dynamically resize the remote desktop size as "
                           "the size of the local client window changes. "
                           "(Does not work with all servers)", true);

BoolParameter viewOnly("ViewOnly",
                       "Don't send any mouse or keyboard events to the server",
                       false);
BoolParameter shared("Shared",
                     "Don't disconnect other viewers upon connection - "
                     "share the desktop instead",
                     false);

BoolParameter acceptClipboard("AcceptClipboard",
                              "Accept clipboard changes from the server",
                              true);
BoolParameter setPrimary("SetPrimary",
                         "Set the primary selection as well as the "
                         "clipboard selection", true);
BoolParameter sendClipboard("SendClipboard",
                            "Send clipboard changes to the server", true);
#if !defined(WIN32) && !defined(__APPLE__)
BoolParameter sendPrimary("SendPrimary",
                          "Send the primary selection to the "
                          "server as well as the clipboard selection",
                          true);
#endif

StringParameter menuKey("MenuKey", "The key which brings up the popup menu",
                        "F8");

BoolParameter fullscreenSystemKeys("FullscreenSystemKeys",
                                   "Pass special keys (like Alt+Tab) directly "
                                   "to the server when in full screen mode.",
                                   true);

