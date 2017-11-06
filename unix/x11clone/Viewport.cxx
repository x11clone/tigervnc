/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright 2011-2014 Pierre Ossman for Cendio AB
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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <rfb/CMsgWriter.h>
#include <rfb/LogWriter.h>
#include <rfb/Exception.h>
#include <rfb/ledStates.h>

// FLTK can pull in the X11 headers on some systems
#ifndef XK_VoidSymbol
#define XK_LATIN1
#define XK_MISCELLANY
#define XK_XKB_KEYS
#include <rfb/keysymdef.h>
#endif

#ifndef XF86XK_ModeLock
#include <rfb/XF86keysym.h>
#endif

#include <X11/XKBlib.h>

#ifndef NoSymbol
#define NoSymbol 0
#endif

#include "Viewport.h"
#include "CConn.h"
#include "OptionsDialog.h"
#include "DesktopWindow.h"
#include "vncviewer/i18n.h"
#include "vncviewer/fltk_layout.h"
#include "parameters.h"
#include "../../vncviewer/menukey.h"
#include "x11clone.h"
#include "../../vncviewer/PlatformPixelBuffer.h"

#include <FL/fl_draw.H>
#include <FL/fl_ask.H>

#include <FL/Fl_Menu.H>
#include <FL/Fl_Menu_Button.H>

#include <X11/XKBlib.h>
extern const struct _code_map_xkb_to_qnum {
  const char * from;
  const unsigned short to;
} code_map_xkb_to_qnum[];
extern const unsigned int code_map_xkb_to_qnum_len;

static int code_map_keycode_to_qnum[256];

using namespace rfb;
using namespace rdr;

static rfb::LogWriter vlog("Viewport");

// Menu constants

enum { ID_EXIT, ID_FULLSCREEN, ID_MINIMIZE, ID_RESIZE,
       ID_CTRL, ID_ALT, ID_MENUKEY, ID_CTRLALTDEL,
       ID_REFRESH, ID_OPTIONS, ID_INFO, ID_ABOUT, ID_DISMISS };

Viewport::Viewport(int w, int h, const rfb::PixelFormat& serverPF, CConn* cc_)
  : Fl_Widget(0, 0, w, h), cc(cc_), frameBuffer(NULL),
    lastPointerPos(0, 0), lastButtonMask(0),
    menuCtrlKey(false), menuAltKey(false), cursor(NULL)
{
  XkbDescPtr xkb;
  Status status;

  xkb = XkbGetMap(fl_display, 0, XkbUseCoreKbd);
  if (!xkb)
    throw rfb::Exception("XkbGetMap");

  status = XkbGetNames(fl_display, XkbKeyNamesMask, xkb);
  if (status != Success)
    throw rfb::Exception("XkbGetNames");

  memset(code_map_keycode_to_qnum, 0, sizeof(code_map_keycode_to_qnum));
  for (KeyCode keycode = xkb->min_key_code;
       keycode < xkb->max_key_code;
       keycode++) {
    const char *keyname = xkb->names->keys[keycode].name;
    unsigned short rfbcode;

    if (keyname[0] == '\0')
      continue;

    rfbcode = 0;
    for (unsigned i = 0;i < code_map_xkb_to_qnum_len;i++) {
        if (strncmp(code_map_xkb_to_qnum[i].from,
                    keyname, XkbKeyNameLength) == 0) {
            rfbcode = code_map_xkb_to_qnum[i].to;
            break;
        }
    }
    if (rfbcode != 0)
        code_map_keycode_to_qnum[keycode] = rfbcode;
    else
        vlog.debug("No key mapping for key %.4s", keyname);
  }

  XkbFreeKeyboard(xkb, 0, True);

  Fl::add_clipboard_notify(handleClipboardChange, this);

  // We need to intercept keyboard events early
  Fl::add_system_handler(handleSystemEvent, this);

  frameBuffer = new PlatformPixelBuffer(w, h);
  assert(frameBuffer);
  cc->setFramebuffer(frameBuffer);

  contextMenu = new Fl_Menu_Button(0, 0, 0, 0);
  // Setting box type to FL_NO_BOX prevents it from trying to draw the
  // button component (which we don't want)
  contextMenu->box(FL_NO_BOX);

  // The (invisible) button associated with this widget can mess with
  // things like Fl_Scroll so we need to get rid of any parents.
  // Unfortunately that's not possible because of STR #2654, but
  // reparenting to the current window works for most cases.
  window()->add(contextMenu);

  setMenuKey();

  OptionsDialog::addCallback(handleOptions, this);

  // Send a fake pointer event so that the server will stop rendering
  // a server-side cursor. Ideally we'd like to send the actual pointer
  // position, but we can't really tell when the window manager is done
  // placing us so we don't have a good time for that.
  handlePointerEvent(Point(w/2, h/2), 0);
}


Viewport::~Viewport()
{
  // Unregister all timeouts in case they get a change tro trigger
  // again later when this object is already gone.
  Fl::remove_timeout(handlePointerTimeout, this);

  Fl::remove_system_handler(handleSystemEvent);

  Fl::remove_clipboard_notify(handleClipboardChange);

  OptionsDialog::removeCallback(handleOptions);

  if (cursor) {
    if (!cursor->alloc_array)
      delete [] cursor->array;
    delete cursor;
  }

  // FLTK automatically deletes all child widgets, so we shouldn't touch
  // them ourselves here
}


const rfb::PixelFormat &Viewport::getPreferredPF()
{
  return frameBuffer->getPF();
}


// Copy the areas of the framebuffer that have been changed (damaged)
// to the displayed window.

void Viewport::updateWindow()
{
  Rect r;

  r = frameBuffer->getDamage();
  damage(FL_DAMAGE_USER1, r.tl.x + x(), r.tl.y + y(), r.width(), r.height());
}

static const char * dotcursor_xpm[] = {
  "5 5 2 1",
  ".	c #000000",
  " 	c #FFFFFF",
  "     ",
  " ... ",
  " ... ",
  " ... ",
  "     "};

void Viewport::setCursor(int width, int height, const Point& hotspot,
                         const rdr::U8* data)
{
  int i;

  if (cursor) {
    if (!cursor->alloc_array)
      delete [] cursor->array;
    delete cursor;
  }

  for (i = 0; i < width*height; i++)
    if (data[i*4 + 3] != 0) break;

  if ((i == width*height) && dotWhenNoCursor) {
    vlog.debug("cursor is empty - using dot");

    Fl_Pixmap pxm(dotcursor_xpm);
    cursor = new Fl_RGB_Image(&pxm);
    cursorHotspot.x = cursorHotspot.y = 2;
  } else {
    if ((width == 0) || (height == 0)) {
      U8 *buffer = new U8[4];
      memset(buffer, 0, 4);
      cursor = new Fl_RGB_Image(buffer, 1, 1, 4);
      cursorHotspot.x = cursorHotspot.y = 0;
    } else {
      U8 *buffer = new U8[width * height * 4];
      memcpy(buffer, data, width * height * 4);
      cursor = new Fl_RGB_Image(buffer, width, height, 4);
      cursorHotspot = hotspot;
    }
  }

  if (Fl::belowmouse() == this)
    window()->cursor(cursor, cursorHotspot.x, cursorHotspot.y);
}


void Viewport::setLEDState(unsigned int state)
{
  Fl_Widget *focus;

  vlog.debug("Got server LED state: 0x%08x", state);

  focus = Fl::grab();
  if (!focus)
    focus = Fl::focus();
  if (!focus)
    return;

  if (focus != this)
    return;

  unsigned int affect, values;
  unsigned int mask;

  Bool ret;

  affect = values = 0;

  affect |= LockMask;
  if (state & ledCapsLock)
    values |= LockMask;

  mask = getModifierMask(XK_Num_Lock);
  affect |= mask;
  if (state & ledNumLock)
    values |= mask;

  mask = getModifierMask(XK_Scroll_Lock);
  affect |= mask;
  if (state & ledScrollLock)
    values |= mask;

  ret = XkbLockModifiers(fl_display, XkbUseCoreKbd, affect, values);
  if (!ret)
    vlog.error(_("Failed to update keyboard LED state"));
}

void Viewport::pushLEDState()
{
  unsigned int state;

  // Server support?
  if (cc->cp.ledState() == ledUnknown)
    return;

  state = 0;

  unsigned int mask;

  Status status;
  XkbStateRec xkbState;

  status = XkbGetState(fl_display, XkbUseCoreKbd, &xkbState);
  if (status != Success) {
    vlog.error(_("Failed to get keyboard LED state: %d"), status);
    return;
  }

  if (xkbState.locked_mods & LockMask)
    state |= ledCapsLock;

  mask = getModifierMask(XK_Num_Lock);
  if (xkbState.locked_mods & mask)
    state |= ledNumLock;

  mask = getModifierMask(XK_Scroll_Lock);
  if (xkbState.locked_mods & mask)
    state |= ledScrollLock;

  if ((state & ledCapsLock) != (cc->cp.ledState() & ledCapsLock)) {
    vlog.debug("Inserting fake CapsLock to get in sync with server");
    handleKeyPress(0x3a, XK_Caps_Lock);
    handleKeyRelease(0x3a);
  }
  if ((state & ledNumLock) != (cc->cp.ledState() & ledNumLock)) {
    vlog.debug("Inserting fake NumLock to get in sync with server");
    handleKeyPress(0x45, XK_Num_Lock);
    handleKeyRelease(0x45);
  }
  if ((state & ledScrollLock) != (cc->cp.ledState() & ledScrollLock)) {
    vlog.debug("Inserting fake ScrollLock to get in sync with server");
    handleKeyPress(0x46, XK_Scroll_Lock);
    handleKeyRelease(0x46);
  }
}


void Viewport::draw(Surface* dst)
{
  int X, Y, W, H;

  // Check what actually needs updating
  fl_clip_box(x(), y(), w(), h(), X, Y, W, H);
  if ((W == 0) || (H == 0))
    return;

  frameBuffer->draw(dst, X - x(), Y - y(), X, Y, W, H);
}


void Viewport::draw()
{
  int X, Y, W, H;

  // Check what actually needs updating
  fl_clip_box(x(), y(), w(), h(), X, Y, W, H);
  if ((W == 0) || (H == 0))
    return;

  frameBuffer->draw(X - x(), Y - y(), X, Y, W, H);
}


void Viewport::resize(int x, int y, int w, int h)
{
  if ((w != frameBuffer->width()) || (h != frameBuffer->height())) {
    vlog.debug("Resizing framebuffer from %dx%d to %dx%d",
               frameBuffer->width(), frameBuffer->height(), w, h);

    frameBuffer = new PlatformPixelBuffer(w, h);
    assert(frameBuffer);
    cc->setFramebuffer(frameBuffer);
  }

  Fl_Widget::resize(x, y, w, h);
}


int Viewport::handle(int event)
{
  char *buffer;
  int ret;
  int buttonMask, wheelMask;
  DownMap::const_iterator iter;

  switch (event) {
  case FL_PASTE:
    buffer = new char[Fl::event_length() + 1];

    // This is documented as to ASCII, but actually does to 8859-1
    ret = fl_utf8toa(Fl::event_text(), Fl::event_length(), buffer,
                     Fl::event_length() + 1);
    assert(ret < (Fl::event_length() + 1));

    vlog.debug("Sending clipboard data (%d bytes)", (int)strlen(buffer));

    try {
      cc->writer()->clientCutText(buffer, ret);
    } catch (rdr::Exception& e) {
      vlog.error("%s", e.str());
      exit_x11clone(e.str());
    }

    delete [] buffer;

    return 1;

  case FL_ENTER:
    if (cursor)
      window()->cursor(cursor, cursorHotspot.x, cursorHotspot.y);
    // Yes, we would like some pointer events please!
    return 1;

  case FL_LEAVE:
    window()->cursor(FL_CURSOR_DEFAULT);
    // Fall through as we want a last move event to help trigger edge stuff
  case FL_PUSH:
  case FL_RELEASE:
  case FL_DRAG:
  case FL_MOVE:
  case FL_MOUSEWHEEL:
    buttonMask = 0;
    if (Fl::event_button1())
      buttonMask |= 1;
    if (Fl::event_button2())
      buttonMask |= 2;
    if (Fl::event_button3())
      buttonMask |= 4;

    if (event == FL_MOUSEWHEEL) {
      wheelMask = 0;
      if (Fl::event_dy() < 0)
        wheelMask |= 8;
      if (Fl::event_dy() > 0)
        wheelMask |= 16;
      if (Fl::event_dx() < 0)
        wheelMask |= 32;
      if (Fl::event_dx() > 0)
        wheelMask |= 64;

      // A quick press of the wheel "button", followed by a immediate
      // release below
      handlePointerEvent(Point(Fl::event_x() - x(), Fl::event_y() - y()),
                         buttonMask | wheelMask);
    } 

    handlePointerEvent(Point(Fl::event_x() - x(), Fl::event_y() - y()), buttonMask);
    return 1;

  case FL_FOCUS:
    Fl::disable_im();
    // Yes, we would like some focus please!
    return 1;

  case FL_UNFOCUS:
    // Release all keys that were pressed as that generally makes most
    // sense (e.g. Alt+Tab where we only see the Alt press)
    while (!downKeySym.empty())
      handleKeyRelease(downKeySym.begin()->first);
    Fl::enable_im();
    return 1;

  case FL_KEYDOWN:
  case FL_KEYUP:
    // Just ignore these as keys were handled in the event handler
    return 1;
  }

  return Fl_Widget::handle(event);
}


unsigned int Viewport::getModifierMask(unsigned int keysym)
{
  XkbDescPtr xkb;
  unsigned int mask, keycode;
  XkbAction *act;

  mask = 0;

  xkb = XkbGetMap(fl_display, XkbAllComponentsMask, XkbUseCoreKbd);
  if (xkb == NULL)
    return 0;

  for (keycode = xkb->min_key_code; keycode <= xkb->max_key_code; keycode++) {
    unsigned int state_out;
    KeySym ks;

    XkbTranslateKeyCode(xkb, keycode, 0, &state_out, &ks);
    if (ks == NoSymbol)
      continue;

    if (ks == keysym)
      break;
  }

  // KeySym not mapped?
  if (keycode > xkb->max_key_code)
    goto out;

  act = XkbKeyAction(xkb, keycode, 0);
  if (act == NULL)
    goto out;
  if (act->type != XkbSA_LockMods)
    goto out;

  if (act->mods.flags & XkbSA_UseModMapMods)
    mask = xkb->map->modmap[keycode];
  else
    mask = act->mods.mask;

out:
  XkbFreeKeyboard(xkb, XkbAllComponentsMask, True);

  return mask;
}


void Viewport::handleClipboardChange(int source, void *data)
{
  Viewport *self = (Viewport *)data;

  assert(self);

  if (!sendClipboard)
    return;

  if (!sendPrimary && (source == 0))
    return;

  Fl::paste(*self, source);
}


void Viewport::handlePointerEvent(const rfb::Point& pos, int buttonMask)
{
  if (!viewOnly) {
    if (pointerEventInterval == 0 || buttonMask != lastButtonMask) {
      try {
        cc->writer()->pointerEvent(pos, buttonMask);
      } catch (rdr::Exception& e) {
        vlog.error("%s", e.str());
        exit_x11clone(e.str());
      }
    } else {
      if (!Fl::has_timeout(handlePointerTimeout, this))
        Fl::add_timeout((double)pointerEventInterval/1000.0,
                        handlePointerTimeout, this);
    }
    lastPointerPos = pos;
    lastButtonMask = buttonMask;
  }
}


void Viewport::handlePointerTimeout(void *data)
{
  Viewport *self = (Viewport *)data;

  assert(self);

  try {
    self->cc->writer()->pointerEvent(self->lastPointerPos, self->lastButtonMask);
  } catch (rdr::Exception& e) {
    vlog.error("%s", e.str());
    exit_x11clone(e.str());
  }
}


void Viewport::handleKeyPress(int keyCode, rdr::U32 keySym)
{
  static bool menuRecursion = false;

  // Prevent recursion if the menu wants to send its own
  // activation key.
  if (menuKeySym && (keySym == menuKeySym) && !menuRecursion) {
    menuRecursion = true;
    popupContextMenu();
    menuRecursion = false;
    return;
  }

  if (viewOnly)
    return;

  if (keyCode == 0) {
    vlog.error(_("No key code specified on key press"));
    return;
  }



  // Because of the way keyboards work, we cannot expect to have the same
  // symbol on release as when pressed. This breaks the VNC protocol however,
  // so we need to keep track of what keysym a key _code_ generated on press
  // and send the same on release.
  downKeySym[keyCode] = keySym;

  vlog.debug("Key pressed: 0x%04x => XK_%s (0x%04x)",
             keyCode, XKeysymToString(keySym), keySym);

  try {
    // Fake keycode?
    if (keyCode > 0xff)
      cc->writer()->keyEvent(keySym, 0, true);
    else
      cc->writer()->keyEvent(keySym, keyCode, true);
  } catch (rdr::Exception& e) {
    vlog.error("%s", e.str());
    exit_x11clone(e.str());
  }

}


void Viewport::handleKeyRelease(int keyCode)
{
  DownMap::iterator iter;

  if (viewOnly)
    return;

  iter = downKeySym.find(keyCode);
  if (iter == downKeySym.end()) {
    // These occur somewhat frequently so let's not spam them unless
    // logging is turned up.
    vlog.debug("Unexpected release of key code %d", keyCode);
    return;
  }

  vlog.debug("Key released: 0x%04x => XK_%s (0x%04x)",
             keyCode, XKeysymToString(iter->second), iter->second);

  try {
    if (keyCode > 0xff)
      cc->writer()->keyEvent(iter->second, 0, false);
    else
      cc->writer()->keyEvent(iter->second, keyCode, false);
  } catch (rdr::Exception& e) {
    vlog.error("%s", e.str());
    exit_x11clone(e.str());
  }

  downKeySym.erase(iter);
}


int Viewport::handleSystemEvent(void *event, void *data)
{
  Viewport *self = (Viewport *)data;
  Fl_Widget *focus;

  assert(self);

  focus = Fl::grab();
  if (!focus)
    focus = Fl::focus();
  if (!focus)
    return 0;

  if (focus != self)
    return 0;

  assert(event);

  XEvent *xevent = (XEvent*)event;

  if (xevent->type == KeyPress) {
    int keycode;
    char str;
    KeySym keysym;

    keycode = code_map_keycode_to_qnum[xevent->xkey.keycode];

    // Generate a fake keycode just for tracking if we can't figure
    // out the proper one
    if (keycode == 0)
        keycode = 0x100 | xevent->xkey.keycode;

    XLookupString(&xevent->xkey, &str, 1, &keysym, NULL);
    if (keysym == NoSymbol) {
      vlog.error(_("No symbol for key code %d (in the current state)"),
                 (int)xevent->xkey.keycode);
    }

    switch (keysym) {
    // For the first few years, there wasn't a good consensus on what the
    // Windows keys should be mapped to for X11. So we need to help out a
    // bit and map all variants to the same key...
    case XK_Hyper_L:
      keysym = XK_Super_L;
      break;
    case XK_Hyper_R:
      keysym = XK_Super_R;
      break;
    // There has been several variants for Shift-Tab over the years.
    // RFB states that we should always send a normal tab.
    case XK_ISO_Left_Tab:
      keysym = XK_Tab;
      break;
    }

    self->handleKeyPress(keycode, keysym);
    return 1;
  } else if (xevent->type == KeyRelease) {
    int keycode = code_map_keycode_to_qnum[xevent->xkey.keycode];
    if (keycode == 0)
        keycode = 0x100 | xevent->xkey.keycode;
    self->handleKeyRelease(keycode);
    return 1;
  }

  return 0;
}

void Viewport::initContextMenu()
{
  contextMenu->clear();

  fltk_menu_add(contextMenu, p_("ContextMenu|", "E&xit x11clone"),
                0, NULL, (void*)ID_EXIT, FL_MENU_DIVIDER);

  fltk_menu_add(contextMenu, p_("ContextMenu|", "&Full screen"),
                0, NULL, (void*)ID_FULLSCREEN,
                FL_MENU_TOGGLE | (window()->fullscreen_active()?FL_MENU_VALUE:0));
  fltk_menu_add(contextMenu, p_("ContextMenu|", "Minimi&ze"),
                0, NULL, (void*)ID_MINIMIZE, 0);
  fltk_menu_add(contextMenu, p_("ContextMenu|", "Resize &window to server display"),
                0, NULL, (void*)ID_RESIZE,
                (window()->fullscreen_active()?FL_MENU_INACTIVE:0) |
                FL_MENU_DIVIDER);

  fltk_menu_add(contextMenu, p_("ContextMenu|", "&Ctrl"),
                0, NULL, (void*)ID_CTRL,
                FL_MENU_TOGGLE | (menuCtrlKey?FL_MENU_VALUE:0));
  fltk_menu_add(contextMenu, p_("ContextMenu|", "&Alt"),
                0, NULL, (void*)ID_ALT,
                FL_MENU_TOGGLE | (menuAltKey?FL_MENU_VALUE:0));

  if (menuKeySym) {
    char sendMenuKey[64];
    snprintf(sendMenuKey, 64, p_("ContextMenu|", "Send %s"), (const char *)menuKey);
    fltk_menu_add(contextMenu, sendMenuKey, 0, NULL, (void*)ID_MENUKEY, 0);
    fltk_menu_add(contextMenu, "Secret shortcut menu key", menuKeyFLTK, NULL,
                  (void*)ID_MENUKEY, FL_MENU_INVISIBLE);
  }

  fltk_menu_add(contextMenu, p_("ContextMenu|", "Send Ctrl-Alt-&Del"),
                0, NULL, (void*)ID_CTRLALTDEL, FL_MENU_DIVIDER);

  fltk_menu_add(contextMenu, p_("ContextMenu|", "&Refresh screen"),
                0, NULL, (void*)ID_REFRESH, FL_MENU_DIVIDER);

  fltk_menu_add(contextMenu, p_("ContextMenu|", "&Options..."),
                0, NULL, (void*)ID_OPTIONS, 0);
  fltk_menu_add(contextMenu, p_("ContextMenu|", "Connection &info..."),
                0, NULL, (void*)ID_INFO, 0);
  fltk_menu_add(contextMenu, p_("ContextMenu|", "About &x11clone..."),
                0, NULL, (void*)ID_ABOUT, FL_MENU_DIVIDER);

  fltk_menu_add(contextMenu, p_("ContextMenu|", "Dismiss &menu"),
                0, NULL, (void*)ID_DISMISS, 0);
}


void Viewport::popupContextMenu()
{
  const Fl_Menu_Item *m;
  char buffer[1024];

  // Make sure the menu is reset to its initial state between goes or
  // it will start up highlighting the previously selected entry.
  contextMenu->value(-1);

  // initialize context menu before display
  initContextMenu();

  // Unfortunately FLTK doesn't reliably restore the mouse pointer for
  // menus, so we have to help it out.
  if (Fl::belowmouse() == this)
    window()->cursor(FL_CURSOR_DEFAULT);

  m = contextMenu->popup();

  // Back to our proper mouse pointer.
  if ((Fl::belowmouse() == this) && cursor)
    window()->cursor(cursor, cursorHotspot.x, cursorHotspot.y);

  if (m == NULL)
    return;

  switch (m->argument()) {
  case ID_EXIT:
    exit_x11clone();
    break;
  case ID_FULLSCREEN:
    if (window()->fullscreen_active())
      window()->fullscreen_off();
    else
      ((DesktopWindow*)window())->fullscreen_on();
    break;
  case ID_MINIMIZE:
    window()->iconize();
    break;
  case ID_RESIZE:
    if (window()->fullscreen_active())
      break;
    window()->size(w(), h());
    break;
  case ID_CTRL:
    if (m->value())
      handleKeyPress(0x1d, XK_Control_L);
    else
      handleKeyRelease(0x1d);
    menuCtrlKey = !menuCtrlKey;
    break;
  case ID_ALT:
    if (m->value())
      handleKeyPress(0x38, XK_Alt_L);
    else
      handleKeyRelease(0x38);
    menuAltKey = !menuAltKey;
    break;
  case ID_MENUKEY:
    handleKeyPress(menuKeyCode, menuKeySym);
    handleKeyRelease(menuKeyCode);
    break;
  case ID_CTRLALTDEL:
    handleKeyPress(0x1d, XK_Control_L);
    handleKeyPress(0x38, XK_Alt_L);
    handleKeyPress(0xd3, XK_Delete);

    handleKeyRelease(0xd3);
    handleKeyRelease(0x38);
    handleKeyRelease(0x1d);
    break;
  case ID_REFRESH:
    cc->refreshFramebuffer();
    break;
  case ID_OPTIONS:
    OptionsDialog::showDialog();
    break;
  case ID_INFO:
    if (fltk_escape(cc->connectionInfo(), buffer, sizeof(buffer)) < sizeof(buffer)) {
      fl_message_title(_("x11clone connection info"));
      fl_message("%s", buffer);
    }
    break;
  case ID_ABOUT:
    about_x11clone();
    break;
  case ID_DISMISS:
    // Don't need to do anything
    break;
  }
}


void Viewport::setMenuKey()
{
  getMenuKey(&menuKeyFLTK, &menuKeyCode, &menuKeySym);
}


void Viewport::handleOptions(void *data)
{
  Viewport *self = (Viewport*)data;

  self->setMenuKey();
}
