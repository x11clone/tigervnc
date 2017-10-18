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

BoolParameter maximize("Maximize", "Maximize window", false);
BoolParameter fullScreen("FullScreen", "Full screen mode", false);
BoolParameter fullScreenAllMonitors("FullScreenAllMonitors",
                                    "Enable full screen over all monitors",
                                    true);
StringParameter desktopSize("DesktopSize",
                            "Reconfigure desktop size on parent display on "
                            "connect (if possible)", "");
StringParameter geometry("geometry",
                         "Specify size and position of window", "");

BoolParameter remoteResize("RemoteResize",
                           "Dynamically resize the remote desktop size as "
                           "the size of the local client window changes. "
                           "(Does not work with all servers)", true);

BoolParameter viewOnly("ViewOnly",
                       "Don't send any mouse or keyboard events to parent display",
                       false);

BoolParameter acceptClipboard("AcceptClipboard",
                              "Accept clipboard changes from parent display",
                              true);
BoolParameter setPrimary("SetPrimary",
                         "Set the primary selection as well as the "
                         "clipboard selection", true);
BoolParameter sendClipboard("SendClipboard",
                            "Send clipboard changes to parent display", true);
BoolParameter sendPrimary("SendPrimary",
                          "Send the primary selection to the "
                          "parent display as well as the clipboard selection",
                          true);

StringParameter menuKey("MenuKey", "The key which brings up the popup menu",
                        "F8");

BoolParameter fullscreenSystemKeys("FullscreenSystemKeys",
                                   "Pass special keys (like Alt+Tab) directly "
                                   "to the parent display when in full screen mode.",
                                   true);

StringParameter parentDisplay("ParentDisplay", "The X display to clone", "");
BoolParameter useShm("UseSHM", "Use MIT-SHM extension if available", true);
IntParameter pollingCycle("PollingCycle", "Milliseconds per one polling "
                          "cycle; actual interval may be dynamically "
                          "adjusted to satisfy MaxProcessorUsage setting", 30);
IntParameter maxProcessorUsage("MaxProcessorUsage", "Maximum percentage of "
                               "CPU time to be consumed", 35);
BoolParameter rawKeyboard("RawKeyboard",
                          "Send keyboard events straight through and "
                          "avoid mapping them to the current keyboard "
                          "layout", false);

