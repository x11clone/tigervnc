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

#ifndef __OPTIONSDIALOG_H__
#define __OPTIONSDIALOG_H__

#include <map>

#include <FL/Fl_Window.H>

class Fl_Widget;
class Fl_Group;
class Fl_Check_Button;
class Fl_Round_Button;
class Fl_Input;
class Fl_Int_Input;
class Fl_Choice;

typedef void (OptionsCallback)(void*);

class OptionsDialog : public Fl_Window {
protected:
  OptionsDialog();
  ~OptionsDialog();

public:
  static void showDialog(void);

  static void addCallback(OptionsCallback *cb, void *data = NULL);
  static void removeCallback(OptionsCallback *cb);

  void show(void);

protected:
  void loadOptions(void);
  void storeOptions(void);

  void createCompressionPage(int tx, int ty, int tw, int th);
  void createInputPage(int tx, int ty, int tw, int th);
  void createScreenPage(int tx, int ty, int tw, int th);
  void createMiscPage(int tx, int ty, int tw, int th);

  static void handleCompression(Fl_Widget *widget, void *data);

  static void handleDesktopSize(Fl_Widget *widget, void *data);

  static void handleClipboard(Fl_Widget *widget, void *data);

  static void handleCancel(Fl_Widget *widget, void *data);
  static void handleOK(Fl_Widget *widget, void *data);

protected:
  static std::map<OptionsCallback*, void*> callbacks;

  /* Input */
  Fl_Check_Button *viewOnlyCheckbox;
  Fl_Check_Button *acceptClipboardCheckbox;
#if !defined(WIN32) && !defined(__APPLE__)
  Fl_Check_Button *setPrimaryCheckbox;
#endif
  Fl_Check_Button *sendClipboardCheckbox;
#if !defined(WIN32) && !defined(__APPLE__)
  Fl_Check_Button *sendPrimaryCheckbox;
#endif
  Fl_Check_Button *systemKeysCheckbox;
  Fl_Choice *menuKeyChoice;

  /* Screen */
  Fl_Check_Button *desktopSizeCheckbox;
  Fl_Int_Input *desktopWidthInput;
  Fl_Int_Input *desktopHeightInput;
  Fl_Check_Button *remoteResizeCheckbox;
  Fl_Check_Button *fullScreenCheckbox;
  Fl_Check_Button *fullScreenAllMonitorsCheckbox;

  /* Misc. */
  Fl_Check_Button *sharedCheckbox;
  Fl_Check_Button *dotCursorCheckbox;
};

#endif
