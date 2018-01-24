.\" brightd manpage
.\" vim:fileencoding=latin-1
.nr no_x11 0
.nr version_maj 0
.nr version_min 4
.nr version_sub 1

.TH brightd 1 "18 October 2008" "\n[version_maj].\n[version_min].\n[version_sub]" "brightd manual"
.SH NAME
brightd \- a brightness control daemon
.SH SYNOPSIS

.ie (\n[no_x11] == 0) \{
.B brightd [-v] [-d] [-P <file>] [-u n] [-e n] [-w n] [-b s] [-f] [-c n] [-x] [-r n]
.\}
.el \{
.B brightd [-v] [-d] [-P <file>] [-u n] [-e n] [-w n] [-b s] [-f] [-c n] [-r n]
.\}

.SH DESCRIPTION
.I brightd
is a daemon which dynamically reduces LCD brightness when you don't use your pc. The idea is adapted from iBooks.
.SH OPTIONS
.TP
-v
Output some debugging information. Will not work
in daemon mode.
.TP
-d
Will cause brightd to fork itself into background.
You'll want to use this ;)
.TP
-P <file>
Set location of pid file in daemon mode (Default is /var/run/brightd.pid).
.TP
-u n
brightd will drop privileges after opening all file descriptors. With this
setting you may choose which user to change to.

You
.B have
.ie (\n[no_x11] == 0) \{\
to start brightd as root; or at least as a user which might as well access
X11-Sessions and
.\}
.el \{\
to start brightd as root; or at least as a user which might access
.\}
.I /dev/input/event
.TP
-e n
Filter used event sources by POSIX extended regexp n (for example, use
"i8042.+event" on intel platforms to avoid having HDAPS taken into account)
You should include "event" here, but you must not do so.
.TP
-w n
The amount of seconds of inactivity to wait before
reducing brightness
.TP
-b n
Dark screen brightness
Never reduce brightness below that value.
Note that you won't be able to change brightness
manually below this value as well.
.TP
-f
Reduce brightness even if on the highest brightness
level.
By default, brightd won't do this. That way you can
temporally disable it while reading through a text or
so.
If you specify this option twice, brightd will also
reduce brightness when you're on AC.
.TP
-c s
Set the backlight class to use. You may specify any subdirectoy of
.I /sys/class/backlight
.if (\n[no_x11] == 0) \{ .TP
-x
Don't query X11 for inactivity / deactivated screensavers
.\}
.TP
-r n
brightd will create a FIFO n (deleting the file if it existed before!) and read
from it. If you tell your acpid to write brightness levels to that FIFO when
the user changes brightness, brightd can help you with some stuff: For example,
if brightd faded to brightness 0 and you increase brightness, brightd would
automatically fade up to the highest level.

.SH FILES
.P 
.I /usr/bin/brightd
.I /sys/class/backlight/*/*

.SH AUTHORS
.nf
Phillip Berndt (mail at pberndt dot com)
Richard Weinberger (richard at nod dot at)
Hannes von Haugwitz (hannes at vonhaugwitz dot com)
.fi
