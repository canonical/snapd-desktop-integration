# snapd-desktop-integration
User session helpers for snapd

## DBus control

The program has a DBus remote control with the interface
io.snapcraft.SnapDesktopIntegration, which offers the following methods:

    ApplicationIsBeingRefreshed(identifier:s, control_file_path:s, extra_data:{s:v})

        Shows a new refresh window for the program 'identifier'. If
        'control_file_path' is empty, the window will be shown until it is
        destroyed with ApplicationRefreshCompleted(), or by the user by
        pressing the "Hide" button in the window. But if it points to
        a file, the window will be destroyed when that file is deleted.

        If there is already a window for that identifier, it will be moved
        to foreground.

    ApplicationRefreshCompleted(identifier:s, extra_data:{s:v})

        Destroys the refresh window for the program specified in 'identifier'.

    ApplicationRefreshPulsed(identifier:s, bar_text:s, extra_data:{s:v})

        Sets the progress bar to "pulsed mode" for the program 'identifier', and
        changes the text in the bar to 'bar_text'.

    ApplicationRefreshPercentage(identifier:s, bar_text:s, percentage:d, extra_data:{s:v})

        Sets the progress bar to "percentage mode" for the program 'identifier', and
        changes the text in the bar to 'bar_text'. The value for the bar is the double
        passed in 'percentage', and it must be a value between 0 and 1.

### Extra data in calls

All the DBus calls accept a dictionary with extra parameters, called 'extra_data'. This
dictionary is in the format 'string:variant' for the 'key:value' data. The available keys
and its associated data are:

* title: the value must be a string variant. It will update the window's title to the
         specified value.

* message: the value must be a string variant. It will replace the message in the window
           (which, by default, is "Please wait while...") with the specified value.

* icon: the value must be a string variant containing an icon name. It will show that icon
        to the right of the message. If the value is an empty string, the icon will be hide.

* icon_image: the value must be a string variant with the path to a picture. It will show
              that picture to the right of the message. Be careful with the picture size.

### Using d-feet

D-feet can be used to call the methods. To pass the 'extra_data' field, it is required to
specify the format in the dictionary.

Example for ApplicationIsBeingRefreshed:

    "Firefox","",{"icon":GLib.Variant("s","firefox")}

    "Telegram","",{"icon_image":GLib.Variant("s","/snap/telegram-desktop/4627/meta/gui/icon.png")}

## Code formatting

Use "clang-format -i SOURCE_FILE_NAME" after any change to automatically
format the code.
