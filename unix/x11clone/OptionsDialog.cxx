/* Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
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

#include <stdlib.h>

#include <list>

#include <rdr/types.h>

#include "OptionsDialog.h"
#include "vncviewer/fltk_layout.h"
#include "vncviewer/i18n.h"
#include "../../vncviewer/menukey.h"
#include "parameters.h"

#include <FL/Fl_Tabs.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Round_Button.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Choice.H>

using namespace std;
using namespace rdr;
using namespace rfb;

std::map<OptionsCallback*, void*> OptionsDialog::callbacks;

OptionsDialog::OptionsDialog()
  : Fl_Window(450, 450, _("x11clone: Connection Options"))
{
  int x, y;
  Fl_Button *button;

  Fl_Tabs *tabs = new Fl_Tabs(OUTER_MARGIN, OUTER_MARGIN,
                             w() - OUTER_MARGIN*2,
                             h() - OUTER_MARGIN*2 - INNER_MARGIN - BUTTON_HEIGHT);

  {
    int tx, ty, tw, th;

    tabs->client_area(tx, ty, tw, th, TABS_HEIGHT);

    createInputPage(tx, ty, tw, th);
    createScreenPage(tx, ty, tw, th);
    createMiscPage(tx, ty, tw, th);
  }

  tabs->end();

  x = w() - BUTTON_WIDTH * 2 - INNER_MARGIN - OUTER_MARGIN;
  y = h() - BUTTON_HEIGHT - OUTER_MARGIN;

  button = new Fl_Button(x, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Cancel"));
  button->callback(this->handleCancel, this);

  x += BUTTON_WIDTH + INNER_MARGIN;

  button = new Fl_Return_Button(x, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("OK"));
  button->callback(this->handleOK, this);

  callback(this->handleCancel, this);

  set_modal();
}


OptionsDialog::~OptionsDialog()
{
}


void OptionsDialog::showDialog(void)
{
  static OptionsDialog *dialog = NULL;

  if (!dialog)
    dialog = new OptionsDialog();

  if (dialog->shown())
    return;

  dialog->show();
}


void OptionsDialog::addCallback(OptionsCallback *cb, void *data)
{
  callbacks[cb] = data;
}


void OptionsDialog::removeCallback(OptionsCallback *cb)
{
  callbacks.erase(cb);
}


void OptionsDialog::show(void)
{
  /* show() gets called for raise events as well */
  if (!shown())
    loadOptions();

  Fl_Window::show();
}


void OptionsDialog::loadOptions(void)
{
  /* Input */
  const char *menuKeyBuf;

  viewOnlyCheckbox->value(viewOnly);
  acceptClipboardCheckbox->value(acceptClipboard);
  setPrimaryCheckbox->value(setPrimary);
  sendClipboardCheckbox->value(sendClipboard);
  sendPrimaryCheckbox->value(sendPrimary);
  systemKeysCheckbox->value(fullscreenSystemKeys);

  menuKeyChoice->value(0);

  menuKeyBuf = menuKey;
  for (int i = 0; i < getMenuKeySymbolCount(); i++)
    if (!strcmp(getMenuKeySymbols()[i].name, menuKeyBuf))
      menuKeyChoice->value(i + 1);

  /* Screen */
  int width, height;

  if (sscanf(desktopSize.getValueStr(), "%dx%d", &width, &height) != 2) {
    desktopSizeCheckbox->value(false);
    desktopWidthInput->value("1024");
    desktopHeightInput->value("768");
  } else {
    char buf[32];
    desktopSizeCheckbox->value(true);
    snprintf(buf, sizeof(buf), "%d", width);
    desktopWidthInput->value(buf);
    snprintf(buf, sizeof(buf), "%d", height);
    desktopHeightInput->value(buf);
  }
  remoteResizeCheckbox->value(remoteResize);
  fullScreenCheckbox->value(fullScreen);
  fullScreenAllMonitorsCheckbox->value(fullScreenAllMonitors);

  handleDesktopSize(desktopSizeCheckbox, this);

  /* Misc. */
  dotCursorCheckbox->value(dotWhenNoCursor);
}


void OptionsDialog::storeOptions(void)
{
  /* Input */
  viewOnly.setParam(viewOnlyCheckbox->value());
  acceptClipboard.setParam(acceptClipboardCheckbox->value());
  setPrimary.setParam(setPrimaryCheckbox->value());
  sendClipboard.setParam(sendClipboardCheckbox->value());
  sendPrimary.setParam(sendPrimaryCheckbox->value());
  fullscreenSystemKeys.setParam(systemKeysCheckbox->value());

  if (menuKeyChoice->value() == 0)
    menuKey.setParam("");
  else {
    menuKey.setParam(menuKeyChoice->text());
  }

  /* Screen */
  int width, height;

  if (desktopSizeCheckbox->value() &&
      (sscanf(desktopWidthInput->value(), "%d", &width) == 1) &&
      (sscanf(desktopHeightInput->value(), "%d", &height) == 1)) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%dx%d", width, height);
    desktopSize.setParam(buf);
  } else {
    desktopSize.setParam("");
  }
  remoteResize.setParam(remoteResizeCheckbox->value());
  fullScreen.setParam(fullScreenCheckbox->value());
  fullScreenAllMonitors.setParam(fullScreenAllMonitorsCheckbox->value());

  /* Misc. */
  dotWhenNoCursor.setParam(dotCursorCheckbox->value());

  std::map<OptionsCallback*, void*>::const_iterator iter;

  for (iter = callbacks.begin();iter != callbacks.end();++iter)
    iter->first(iter->second);
}


void OptionsDialog::createInputPage(int tx, int ty, int tw, int th)
{
  Fl_Group *group = new Fl_Group(tx, ty, tw, th, _("Input"));

  tx += OUTER_MARGIN;
  ty += OUTER_MARGIN;

  viewOnlyCheckbox = new Fl_Check_Button(LBLRIGHT(tx, ty,
                                                  CHECK_MIN_WIDTH,
                                                  CHECK_HEIGHT,
                                                  _("View only (ignore mouse and keyboard)")));
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

  acceptClipboardCheckbox = new Fl_Check_Button(LBLRIGHT(tx, ty,
                                                         CHECK_MIN_WIDTH,
                                                         CHECK_HEIGHT,
                                                         _("Accept clipboard from parent display")));
  acceptClipboardCheckbox->callback(handleClipboard, this);
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

  setPrimaryCheckbox = new Fl_Check_Button(LBLRIGHT(tx + INDENT, ty,
                                                    CHECK_MIN_WIDTH,
                                                    CHECK_HEIGHT,
                                                    _("Also set primary selection")));
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

  sendClipboardCheckbox = new Fl_Check_Button(LBLRIGHT(tx, ty,
                                                       CHECK_MIN_WIDTH,
                                                       CHECK_HEIGHT,
                                                       _("Send clipboard to parent display")));
  sendClipboardCheckbox->callback(handleClipboard, this);
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

  sendPrimaryCheckbox = new Fl_Check_Button(LBLRIGHT(tx + INDENT, ty,
                                                     CHECK_MIN_WIDTH,
                                                     CHECK_HEIGHT,
                                                     _("Send primary selection as clipboard")));
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

  systemKeysCheckbox = new Fl_Check_Button(LBLRIGHT(tx, ty,
                                                    CHECK_MIN_WIDTH,
                                                    CHECK_HEIGHT,
                                                    _("Pass system keys directly to parent display (full screen)")));
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

  menuKeyChoice = new Fl_Choice(LBLLEFT(tx, ty, 150, CHOICE_HEIGHT, _("Menu key")));

  fltk_menu_add(menuKeyChoice, _("None"), 0, NULL, (void*)0, FL_MENU_DIVIDER);
  for (int i = 0; i < getMenuKeySymbolCount(); i++)
    fltk_menu_add(menuKeyChoice, getMenuKeySymbols()[i].name, 0, NULL, 0, 0);

  ty += CHOICE_HEIGHT + TIGHT_MARGIN;

  group->end();
}


void OptionsDialog::createScreenPage(int tx, int ty, int tw, int th)
{
  int x;

  Fl_Group *group = new Fl_Group(tx, ty, tw, th, _("Screen"));

  tx += OUTER_MARGIN;
  ty += OUTER_MARGIN;

  desktopSizeCheckbox = new Fl_Check_Button(LBLRIGHT(tx, ty,
                                                     CHECK_MIN_WIDTH,
                                                     CHECK_HEIGHT,
                                                     _("Resize parent display on connect")));
  desktopSizeCheckbox->callback(handleDesktopSize, this);
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

  desktopWidthInput = new Fl_Int_Input(tx + INDENT, ty, 50, INPUT_HEIGHT);
  x = desktopWidthInput->x() + desktopWidthInput->w() + \
      gui_str_len("x") + 3 * 2;
  desktopHeightInput = new Fl_Int_Input(x, ty, 50, INPUT_HEIGHT, "x");
  ty += INPUT_HEIGHT + TIGHT_MARGIN;

  remoteResizeCheckbox = new Fl_Check_Button(LBLRIGHT(tx, ty,
                                                      CHECK_MIN_WIDTH,
                                                      CHECK_HEIGHT,
                                                      _("Resize parent display to the local window")));
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

  fullScreenCheckbox = new Fl_Check_Button(LBLRIGHT(tx, ty,
                                                  CHECK_MIN_WIDTH,
                                                  CHECK_HEIGHT,
                                                  _("Full-screen mode")));
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

  fullScreenAllMonitorsCheckbox = new Fl_Check_Button(LBLRIGHT(tx + INDENT, ty,
                                                      CHECK_MIN_WIDTH,
                                                      CHECK_HEIGHT,
                                                      _("Enable full-screen mode over all monitors")));
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

  group->end();
}


void OptionsDialog::createMiscPage(int tx, int ty, int tw, int th)
{
  Fl_Group *group = new Fl_Group(tx, ty, tw, th, _("Misc."));

  tx += OUTER_MARGIN;
  ty += OUTER_MARGIN;

  dotCursorCheckbox = new Fl_Check_Button(LBLRIGHT(tx, ty,
                                                  CHECK_MIN_WIDTH,
                                                  CHECK_HEIGHT,
                                                  _("Show dot when no cursor")));
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

  group->end();
}

void OptionsDialog::handleDesktopSize(Fl_Widget *widget, void *data)
{
  OptionsDialog *dialog = (OptionsDialog*)data;

  if (dialog->desktopSizeCheckbox->value()) {
    dialog->desktopWidthInput->activate();
    dialog->desktopHeightInput->activate();
  } else {
    dialog->desktopWidthInput->deactivate();
    dialog->desktopHeightInput->deactivate();
  }
}

void OptionsDialog::handleClipboard(Fl_Widget *widget, void *data)
{
  OptionsDialog *dialog = (OptionsDialog*)data;

  if (dialog->acceptClipboardCheckbox->value())
    dialog->setPrimaryCheckbox->activate();
  else
    dialog->setPrimaryCheckbox->deactivate();
  if (dialog->sendClipboardCheckbox->value())
    dialog->sendPrimaryCheckbox->activate();
  else
    dialog->sendPrimaryCheckbox->deactivate();
}

void OptionsDialog::handleCancel(Fl_Widget *widget, void *data)
{
  OptionsDialog *dialog = (OptionsDialog*)data;

  dialog->hide();
}


void OptionsDialog::handleOK(Fl_Widget *widget, void *data)
{
  OptionsDialog *dialog = (OptionsDialog*)data;

  dialog->hide();

  dialog->storeOptions();
}
