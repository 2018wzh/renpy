# Copyright 2026 AstraEngine contributors. SPDX-License-Identifier: MIT
# This is a real Ren'Py project, also runnable with the unmodified official SDK.
define config.name = "Astra Ren'Py Native Smoke"
define config.version = "1.0"
define config.screen_width = 640
define config.screen_height = 360
define config.physical_width = 640
define config.physical_height = 360
define config.window_title = "Astra Ren'Py Native Smoke"
define config.save_directory = "astra-renpy-native-smoke"
define config.has_autosave = False
define config.autosave_on_choice = False
define config.autosave_on_input = False
define config.developer = False
default smoke_value = 0
default smoke_name = ""

init 10 python:
    gui.init(640, 360)

transform native_pulse:
    alpha 1.0
    linear 0.8 alpha 0.15
    linear 0.8 alpha 1.0
    repeat

screen main_menu():
    tag menu
    add Solid("#172a4d")
    text "Official Ren'Py runtime" xalign 0.5 ypos 32 size 28
    button:
        area (160, 100, 320, 80)
        background Solid("#269653")
        text "START" align (0.5, 0.5) size 24
        action Start()
    text "Python / Screen Language / GL2" xalign 0.5 ypos 240 size 20

screen native_choice():
    modal True
    add Solid("#3b2554")
    key "K_RETURN" action Return("right")
    text "Native Screen Language choice" xalign 0.5 ypos 32 size 28
    button:
        area (40, 100, 240, 80)
        background Solid("#ba472c")
        text "LEFT" align (0.5, 0.5) size 24
        action Return("left")
    button:
        area (360, 100, 240, 80)
        background Solid("#287eb5")
        text "RIGHT" align (0.5, 0.5) size 24
        action Return("right")

screen input(prompt):
    modal True
    add Solid("#174b5b")
    text prompt xalign 0.5 ypos 40 size 28
    input id "input" xpos 100 ypos 140 xsize 440 size 28
    text "Enter to continue" xalign 0.5 ypos 260 size 20

screen native_storage():
    modal True
    add Solid("#195b38" if smoke_value == 7 and smoke_name == "Astra Ω" else "#852b36")
    text "SAVE / CHANGE / LOAD" xalign 0.5 ypos 26 size 28
    text "Value: [smoke_value]" xalign 0.5 ypos 66 size 22
    text "Name: [smoke_name]" xalign 0.5 ypos 215 size 20
    add Solid("#f2cc60") xpos 600 ypos 320 xsize 20 ysize 20 at native_pulse
    button:
        area (40, 120, 140, 70)
        background Solid("#355b9c")
        text "SAVE" align (0.5, 0.5) size 22
        action FileSave("1", confirm=False)
    button:
        area (250, 120, 140, 70)
        background Solid("#805827")
        text "CHANGE" align (0.5, 0.5) size 22
        action SetVariable("smoke_value", 42)
    button:
        area (460, 120, 140, 70)
        background Solid("#6c4196")
        text "LOAD" align (0.5, 0.5) size 22
        action FileLoad("1", confirm=False)
    button:
        area (250, 250, 140, 60)
        background Solid("#323b45")
        text "QUIT" align (0.5, 0.5) size 22
        action Quit(confirm=False)

label start:
    play music "<silence 60>" loop
    call screen native_choice
    if _return == "right":
        $ smoke_value = 7
    else:
        $ smoke_value = 3
    $ smoke_name = renpy.input("Name", default="", length=32).strip()
    call screen native_storage
    return
