Screenshots of the shared dialogs as tests/settings/run.py takes them from SETDEMO.PRG (9 Nov 2026, hub dialogs).
For looking at: nothing compares them pixel by pixel, except what run.py says it compares (the ring gone, the dialog as
opened).

radios-*    SETDEMO's key 3, an EPlay-like Output page (ui/settings.c: set_radio, set_radio_enable, set_popup_enable,
            set_choice_when; the keyboard's reach): tab-ring (Tab to the radio group: a ring round the chosen button),
            dsp-chosen (Right, Right), dsp-greyed (the tick "no DSP" turned on by Space: DSP greyed, still chosen),
            popup-greyed-item (the pop-up opened by Space, its first item greyed), list-ring (the page list in focus).
            st-mono: TOS 1.04, flat, the programs' flags (-DDLG_CPU -DDLG_CLICK); falcon4-16: TOS 4.04, 3D.
font-*      SETDEMO's key 4, the Font dialog (ui/fontsel.c): the title band " Font", Cancel then OK, 10 wide. In a window
            under XaAES the window's own title replaces the band (font-xaaes-window).
keys-xaaes-page-plain   the Settings page list's Down in a window under XaAES.
menu-xaaes-*   MD17 under FreeMiNT with XaAES: a menu item chosen while the dialog window is open. dropdown-over-dialog is
            the menu open over it, dropped-title-normal the title shown normal again after the choice (dropped: nothing is
            run, not even when the dialog closes); OLD-title-left-highlighted what the old dialog.c did (it kept the choice,
            ran it after the dialog closed and left the title highlighted).

The Languages dialog's pictures are in ../langs/shots.
