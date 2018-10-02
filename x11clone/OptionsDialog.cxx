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
#include <rfb/encodings.h>

#include "OptionsDialog.h"
#include "fltk_layout.h"
#include "i18n.h"
#include "menukey.h"
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

    createCompressionPage(tx, ty, tw, th);
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
  /* Compression */
  autoselectCheckbox->value(autoSelect);

  int encNum = encodingNum(preferredEncoding);

  switch (encNum) {
  case encodingTight:
    tightButton->setonly();
    break;
  case encodingZRLE:
    zrleButton->setonly();
    break;
  case encodingHextile:
    hextileButton->setonly();
    break;
  case encodingRaw:
    rawButton->setonly();
    break;
  }

  if (fullColour)
    fullcolorCheckbox->setonly();
  else {
    switch (lowColourLevel) {
    case 0:
      verylowcolorCheckbox->setonly();
      break;
    case 1:
      lowcolorCheckbox->setonly();
      break;
    case 2:
      mediumcolorCheckbox->setonly();
      break;
    }
  }

  char digit[2] = "0";

  compressionCheckbox->value(customCompressLevel);
  jpegCheckbox->value(!noJpeg);
  digit[0] = '0' + compressLevel;
  compressionInput->value(digit);
  digit[0] = '0' + qualityLevel;
  jpegInput->value(digit);

  handleAutoselect(autoselectCheckbox, this);
  handleCompression(compressionCheckbox, this);
  handleJpeg(jpegCheckbox, this);

  /* Input */
  const char *menuKeyBuf;

  viewOnlyCheckbox->value(viewOnly);
  acceptClipboardCheckbox->value(acceptClipboard);
#if !defined(WIN32) && !defined(__APPLE__)
  setPrimaryCheckbox->value(setPrimary);
#endif
  sendClipboardCheckbox->value(sendClipboard);
#if !defined(WIN32) && !defined(__APPLE__)
  sendPrimaryCheckbox->value(sendPrimary);
#endif
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
  /* Compression */
  autoSelect.setParam(autoselectCheckbox->value());

  if (tightButton->value())
    preferredEncoding.setParam(encodingName(encodingTight));
  else if (zrleButton->value())
    preferredEncoding.setParam(encodingName(encodingZRLE));
  else if (hextileButton->value())
    preferredEncoding.setParam(encodingName(encodingHextile));
  else if (rawButton->value())
    preferredEncoding.setParam(encodingName(encodingRaw));

  fullColour.setParam(fullcolorCheckbox->value());
  if (verylowcolorCheckbox->value())
    lowColourLevel.setParam(0);
  else if (lowcolorCheckbox->value())
    lowColourLevel.setParam(1);
  else if (mediumcolorCheckbox->value())
    lowColourLevel.setParam(2);

  customCompressLevel.setParam(compressionCheckbox->value());
  noJpeg.setParam(!jpegCheckbox->value());
  compressLevel.setParam(atoi(compressionInput->value()));
  qualityLevel.setParam(atoi(jpegInput->value()));

  /* Input */
  viewOnly.setParam(viewOnlyCheckbox->value());
  acceptClipboard.setParam(acceptClipboardCheckbox->value());
#if !defined(WIN32) && !defined(__APPLE__)
  setPrimary.setParam(setPrimaryCheckbox->value());
#endif
  sendClipboard.setParam(sendClipboardCheckbox->value());
#if !defined(WIN32) && !defined(__APPLE__)
  sendPrimary.setParam(sendPrimaryCheckbox->value());
#endif
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


void OptionsDialog::createCompressionPage(int tx, int ty, int tw, int th)
{
  Fl_Group *group = new Fl_Group(tx, ty, tw, th, _("Compression"));

  int orig_tx, orig_ty;
  int half_width, full_width;
  int height;

  tx += OUTER_MARGIN;
  ty += OUTER_MARGIN;

  full_width = tw - OUTER_MARGIN * 2;
  half_width = (full_width - INNER_MARGIN) / 2;

  /* AutoSelect checkbox */
  autoselectCheckbox = new Fl_Check_Button(LBLRIGHT(tx, ty,
                                                     CHECK_MIN_WIDTH,
                                                     CHECK_HEIGHT,
                                                     _("Auto select")));
  autoselectCheckbox->callback(handleAutoselect, this);
  ty += CHECK_HEIGHT + INNER_MARGIN;

  /* Two columns */
  orig_tx = tx;
  orig_ty = ty;

  /* VNC encoding box */
  ty += GROUP_LABEL_OFFSET;
  height = GROUP_MARGIN * 2 + TIGHT_MARGIN * 3 + RADIO_HEIGHT * 4;
  encodingGroup = new Fl_Group(tx, ty, half_width, height,
                                _("Preferred encoding"));
  encodingGroup->box(FL_ENGRAVED_BOX);
  encodingGroup->align(FL_ALIGN_LEFT | FL_ALIGN_TOP);

  {
    tx += GROUP_MARGIN;
    ty += GROUP_MARGIN;

    tightButton = new Fl_Round_Button(LBLRIGHT(tx, ty,
                                               RADIO_MIN_WIDTH,
                                               RADIO_HEIGHT,
                                               "Tight"));
    tightButton->type(FL_RADIO_BUTTON);
    ty += RADIO_HEIGHT + TIGHT_MARGIN;

    zrleButton = new Fl_Round_Button(LBLRIGHT(tx, ty,
                                              RADIO_MIN_WIDTH,
                                              RADIO_HEIGHT,
                                              "ZRLE"));
    zrleButton->type(FL_RADIO_BUTTON);
    ty += RADIO_HEIGHT + TIGHT_MARGIN;

    hextileButton = new Fl_Round_Button(LBLRIGHT(tx, ty,
                                                 RADIO_MIN_WIDTH,
                                                 RADIO_HEIGHT,
                                                 "Hextile"));
    hextileButton->type(FL_RADIO_BUTTON);
    ty += RADIO_HEIGHT + TIGHT_MARGIN;

    rawButton = new Fl_Round_Button(LBLRIGHT(tx, ty,
                                             RADIO_MIN_WIDTH,
                                             RADIO_HEIGHT,
                                             "Raw"));
    rawButton->type(FL_RADIO_BUTTON);
    ty += RADIO_HEIGHT + TIGHT_MARGIN;
  }

  ty += GROUP_MARGIN - TIGHT_MARGIN;

  encodingGroup->end();

  /* Second column */
  tx = orig_tx + half_width + INNER_MARGIN;
  ty = orig_ty;

  /* Color box */
  ty += GROUP_LABEL_OFFSET;
  height = GROUP_MARGIN * 2 + TIGHT_MARGIN * 3 + RADIO_HEIGHT * 4;
  colorlevelGroup = new Fl_Group(tx, ty, half_width, height, _("Color level"));
  colorlevelGroup->box(FL_ENGRAVED_BOX);
  colorlevelGroup->align(FL_ALIGN_LEFT | FL_ALIGN_TOP);

  {
    tx += GROUP_MARGIN;
    ty += GROUP_MARGIN;

    fullcolorCheckbox = new Fl_Round_Button(LBLRIGHT(tx, ty,
                                                     RADIO_MIN_WIDTH,
                                                     RADIO_HEIGHT,
                                                     _("Full (all available colors)")));
    fullcolorCheckbox->type(FL_RADIO_BUTTON);
    ty += RADIO_HEIGHT + TIGHT_MARGIN;

    mediumcolorCheckbox = new Fl_Round_Button(LBLRIGHT(tx, ty,
                                                       RADIO_MIN_WIDTH,
                                                       RADIO_HEIGHT,
                                                       _("Medium (256 colors)")));
    mediumcolorCheckbox->type(FL_RADIO_BUTTON);
    ty += RADIO_HEIGHT + TIGHT_MARGIN;

    lowcolorCheckbox = new Fl_Round_Button(LBLRIGHT(tx, ty,
                                                    RADIO_MIN_WIDTH,
                                                    RADIO_HEIGHT,
                                                    _("Low (64 colors)")));
    lowcolorCheckbox->type(FL_RADIO_BUTTON);
    ty += RADIO_HEIGHT + TIGHT_MARGIN;

    verylowcolorCheckbox = new Fl_Round_Button(LBLRIGHT(tx, ty,
                                                        RADIO_MIN_WIDTH,
                                                        RADIO_HEIGHT,
                                                        _("Very low (8 colors)")));
    verylowcolorCheckbox->type(FL_RADIO_BUTTON);
    ty += RADIO_HEIGHT + TIGHT_MARGIN;
  }

  ty += GROUP_MARGIN - TIGHT_MARGIN;

  colorlevelGroup->end();

  /* Back to normal */
  tx = orig_tx;
  ty += INNER_MARGIN;

  /* Checkboxes */
  compressionCheckbox = new Fl_Check_Button(LBLRIGHT(tx, ty,
                                                     CHECK_MIN_WIDTH,
                                                     CHECK_HEIGHT,
                                                     _("Custom compression level:")));
  compressionCheckbox->callback(handleCompression, this);
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

  compressionInput = new Fl_Int_Input(tx + INDENT, ty,
                                      INPUT_HEIGHT, INPUT_HEIGHT,
                                      _("level (1=fast, 6=best [4-6 are rarely useful])"));
  compressionInput->align(FL_ALIGN_RIGHT);
  ty += INPUT_HEIGHT + INNER_MARGIN;

  jpegCheckbox = new Fl_Check_Button(LBLRIGHT(tx, ty,
                                              CHECK_MIN_WIDTH,
                                              CHECK_HEIGHT,
                                              _("Allow JPEG compression:")));
  jpegCheckbox->callback(handleJpeg, this);
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

  jpegInput = new Fl_Int_Input(tx + INDENT, ty,
                               INPUT_HEIGHT, INPUT_HEIGHT,
                               _("quality (0=poor, 9=best)"));
  jpegInput->align(FL_ALIGN_RIGHT);
  ty += INPUT_HEIGHT + INNER_MARGIN;

  group->end();
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
                                                         _("Accept clipboard from server")));
  acceptClipboardCheckbox->callback(handleClipboard, this);
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

#if !defined(WIN32) && !defined(__APPLE__)
  setPrimaryCheckbox = new Fl_Check_Button(LBLRIGHT(tx + INDENT, ty,
                                                    CHECK_MIN_WIDTH,
                                                    CHECK_HEIGHT,
                                                    _("Also set primary selection")));
  ty += CHECK_HEIGHT + TIGHT_MARGIN;
#endif

  sendClipboardCheckbox = new Fl_Check_Button(LBLRIGHT(tx, ty,
                                                       CHECK_MIN_WIDTH,
                                                       CHECK_HEIGHT,
                                                       _("Send clipboard to server")));
  sendClipboardCheckbox->callback(handleClipboard, this);
  ty += CHECK_HEIGHT + TIGHT_MARGIN;

#if !defined(WIN32) && !defined(__APPLE__)
  sendPrimaryCheckbox = new Fl_Check_Button(LBLRIGHT(tx + INDENT, ty,
                                                     CHECK_MIN_WIDTH,
                                                     CHECK_HEIGHT,
                                                     _("Send primary selection as clipboard")));
  ty += CHECK_HEIGHT + TIGHT_MARGIN;
#endif

  systemKeysCheckbox = new Fl_Check_Button(LBLRIGHT(tx, ty,
                                                    CHECK_MIN_WIDTH,
                                                    CHECK_HEIGHT,
                                                    _("Pass system keys directly to server (full screen)")));
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
                                                     _("Resize server display on connect")));
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
                                                      _("Resize server display to the local window")));
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


void OptionsDialog::handleAutoselect(Fl_Widget *widget, void *data)
{
  OptionsDialog *dialog = (OptionsDialog*)data;

  if (dialog->autoselectCheckbox->value()) {
    dialog->encodingGroup->deactivate();
    dialog->colorlevelGroup->deactivate();
  } else {
    dialog->encodingGroup->activate();
    dialog->colorlevelGroup->activate();
  }

  // JPEG setting is also affected by autoselection
  dialog->handleJpeg(dialog->jpegCheckbox, dialog);
}


void OptionsDialog::handleCompression(Fl_Widget *widget, void *data)
{
  OptionsDialog *dialog = (OptionsDialog*)data;

  if (dialog->compressionCheckbox->value())
    dialog->compressionInput->activate();
  else
    dialog->compressionInput->deactivate();
}


void OptionsDialog::handleJpeg(Fl_Widget *widget, void *data)
{
  OptionsDialog *dialog = (OptionsDialog*)data;

  if (dialog->jpegCheckbox->value() &&
      !dialog->autoselectCheckbox->value())
    dialog->jpegInput->activate();
  else
    dialog->jpegInput->deactivate();
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
#if !defined(WIN32) && !defined(__APPLE__)
  OptionsDialog *dialog = (OptionsDialog*)data;

  if (dialog->acceptClipboardCheckbox->value())
    dialog->setPrimaryCheckbox->activate();
  else
    dialog->setPrimaryCheckbox->deactivate();
  if (dialog->sendClipboardCheckbox->value())
    dialog->sendPrimaryCheckbox->activate();
  else
    dialog->sendPrimaryCheckbox->deactivate();
#endif
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
