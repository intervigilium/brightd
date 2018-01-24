/**
 * Brightness control daemon
 * Copyright (c) 2006-2008, Phillip Berndt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Compile using:
 *  gcc -lX11 -lXss -DX11 brightd.c or
 *  gcc brightd.c		    or
 *  use make
 *
 */
#define RELEASE "0.4.1"
/* #define DEBUG */
/* #define X11 */

/* Includes {{{ */
#include <unistd.h>
#include <signal.h>
#include <string.h>
#ifdef X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/scrnsaver.h>
#endif
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <glob.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <pwd.h>
#include <time.h>
#include <regex.h>
#include <sys/inotify.h>
/* }}} */

/* Overall program settings {{{ */
#ifdef DEBUG
	char verbose = 1;
#else
	char verbose = 0;
#endif
char daemonize	 = 0;
char force	 = 0;
int  waitSeconds = 3;
int  darkBright  = 0;
int  maxBright	 = 0;

char isDark	 = 0;
char savedLevel  = 0;
char beforeFade  = 0;

char pidfile[255];

time_t lastFade  = 0;

char b_class[21];
char actualBrightnessFile[255];
char brightnessFile[255];

char b_fifo[255];
int  bfifoFd	       = 0;

FILE *brightnessFd;

regex_t eventSourceFilter;

#ifdef X11
Display *display = NULL;
#endif

/* Required prototypes */
void signalHandlerAlarm(int sig);
/* }}} */


/*
 * Write an error to stderr and exit
 */
void error(char *str) { /* {{{ */
	if(daemonize != 1) {
		fputs(str, stderr);
		fputs("\n", stderr);

		#ifdef DEBUG
		#include <errno.h>
		printf("libc says: %s\n", strerror(errno));
		#endif
	}

	#ifdef X11
		/* Close display */
		if(display) {
			XCloseDisplay(display);
		}
	#endif

	/* Quit */
	exit(1);
} /* }}} */

/*
 * Write an debug message to the console
 */
void info(char *str) { /*{{{*/
	if(verbose == 1 && daemonize == 0) {
		printf("%s\n", str);
	}
} /*}}}*/

/*
 * Function to get the brightness of the pc
 */
int getBrightness() { /*{{{*/
	char line[255];
	int retVal;
	FILE *brightness = fopen(actualBrightnessFile, "r");
	if(!brightness) {
		error("Failed to open /sys/class/backlight/*/actual_brightness. Do you have the "
			"ACPI extensions for the class supplied using -c in your kernel?");
	}
	fgets(line, 255, brightness);
	retVal = atoi(line);
	fclose(brightness);

	return retVal;
} /*}}}*/

/*
 * Function to set the brightness of the
 * display
 * 
 * Must be done stepwise!
 */
void setBrightness(int level) { /*{{{*/
	char output[255];
	int i, plevel;
	int saved;

	saved = alarm(0);
	signal(SIGALRM, SIG_IGN);
	lastFade = time(NULL);
	plevel = getBrightness();
	for(i=plevel; ; i += (plevel<level ? 1 : -1)) {
		sprintf(output, "%i\n", i);
		if(fputs(output, brightnessFd) == EOF) {
			error("Failed to update brightness.");
		}
		fflush(brightnessFd);

		if(i == level) {
			break;
		}

		/* We had to disable signal handler and alarm because of this: */
		usleep(200);
	}
	signal(SIGALRM, signalHandlerAlarm);
	alarm(saved);
} /*}}}*/

/*
 * Check whether the PC is on AC
 */
int isOnAC() /*{{{*/
{
	char line[255];
	FILE *ac = fopen("/proc/acpi/ac_adapter/AC/state", "r");
	if(!ac) {
		/* Ignore this */
		return 0;
	}
	fgets(line, 255, ac);
	fclose(ac);

	return strstr(line, "on-line") != NULL;
} /*}}}*/

/*
 * Signal handler; just quit
 */
void signalHandlerQuit(int signal) { /*{{{*/
	#ifdef X11
	if(display != NULL) {
		info("Closing display");
		XCloseDisplay(display);
	}
	#endif
	if(bfifoFd != 0) {
		/* This may or may not work */
		close(bfifoFd);
		if(unlink(b_fifo) != 0) {
			info("Failed to remove FIFO - this does only work when you didn't drop privileges");
		}
	}
	error("Received signal. Exiting...");
} /*}}}*/

#ifdef X11
/*
 * Get IDLE time
 * Taken from GAJIM source, src/common/idle.c
 *
 * Modified to return -1 when the screen saver is deactivated
 */
int getIdleTime() { /*{{{*/
	static XScreenSaverInfo *mit_info = NULL;
	int idle_time, event_base, error_base;

	if (XScreenSaverQueryExtension(display, &event_base, &error_base)) {
		if (mit_info == NULL) {
			mit_info = XScreenSaverAllocInfo();
		}
		XScreenSaverQueryInfo(display, RootWindow(display, 0), mit_info);

		if(mit_info->state == 3) { /* ScreenSaverDisabled */
			idle_time = -1;
		}
		else {
			idle_time = (mit_info->idle) / 1000;
		}
	}
	else {
		idle_time = 0;
	}
	return idle_time;
} /*}}}*/
#endif

/*
 * Print a help message and exit
 */
void printHelp() /*{{{*/
{
	#ifdef X11
	printf("brightd %s\nA X11-daemon for iBook-like brightness management\nCopyright (c) 2006-2008, Phillip Berndt\n\nOptions:\n", RELEASE);
	#else
	printf("brightd %s\nA daemon for iBook-like brightness management\nCopyright (c) 2006-2008, Phillip Berndt\n\nOptions:\n", RELEASE);
	#endif
	printf(" -v	   Be verbose\n");
	printf(" -d	   Daemonize\n");
	printf(" -P <file> Create pid file\n");
	printf(" -u n	   Drop privileges to this user (Defaults to nobody)\n");
	printf(" -w n	   Wait n seconds before reducing brightness (Defaults to 3)\n");
	printf(" -b n	   The brightness setting for the dark screen (Defaults zu 0)\n");
	printf(" -f	   Reduce brightness even if on the highest brightness level\n");
	printf("	   Specify twice to also do so when on AC\n");
	printf(" -e n	   Filter event sources using regexp n (on /dev/input/by-path\n");
	printf(" -c n	   Set the backlight class to use (defaults to the first\n");
	printf("	   subnode of /sys/class/backlight)\n");
	#ifdef X11
	printf(" -x	   Don't query X11 Xss extension\n");
	#endif
	printf(" -r n	   Create a FIFO, into which acpid may write the new level when the user\n");
	printf("	   changed display brightness\n");
	printf("\n");
	exit(0);
} /*}}}*/

/*
 * Load default brightness class into b_class
 */
void loadDefaultClass() /*{{{*/
{
	struct dirent *dirEntry;
	DIR *backlightDir = opendir("/sys/class/backlight");
	if(backlightDir) {
		while(1) {
			dirEntry = readdir(backlightDir);
			if(!dirEntry) {
				break;
			}
			if(dirEntry->d_name[0] == '.') {
				continue;
			}
			strcpy(b_class, dirEntry->d_name);
			if(verbose == 1) {
				printf("Using brightness class %s\n", b_class);
			}
			closedir(backlightDir);
			return;
		}
		closedir(backlightDir);
	}
	strcpy(b_class, "none");
} /*}}}*/

/*
 * Event source filter
 */
char isEventFileValid(char *file) { /* {{{ */
	if(*((char*)&eventSourceFilter) == 0) {
		return 1;
	}
	else {
		return regexec(&eventSourceFilter, file, 0, NULL, 0) == 0;
	}
} /* }}} */

/**
 * Application logic code {{{
 */
void signalHandlerAlarm(int sig) { /*{{{*/
	/* Reenable the handler */
	signal(sig, signalHandlerAlarm);

	if(isDark == 0) {
		/* Check if fading is okay */
		if(isOnAC() && force < 2) {
			info("Would fade, but on AC");
			alarm(waitSeconds);
			return;
		}
		if(getBrightness() == maxBright && force < 1) {
			info("Would fade, but maximum brightness level selected");
			alarm(waitSeconds);
			return;
		}
	#ifdef X11
		if(display != NULL && getIdleTime() < waitSeconds) {
			info("Would fade, but screensaver denies to do so");
			alarm(waitSeconds);
			return;
		}
	#endif

		/* Fade */
		info("Fading to dark");
		beforeFade = getBrightness();
		savedLevel = darkBright;
		setBrightness(darkBright);
		isDark = 1;
	}
	return;
} /*}}}*/

void handleReceivedEvent() { /*{{{*/
	int level;

	/* Reset alarm */
	alarm(waitSeconds);

	/* Fade back to light */
	if(isDark == 1) {
		level = getBrightness();
		if(level > beforeFade) {
			savedLevel = level;
			isDark = 0;
			return;
		}
		if(verbose == 1 && daemonize == 0) {
			printf("Fading to light (level %d)\n", beforeFade);
		}
		savedLevel = beforeFade;
		setBrightness(beforeFade);
		isDark = 0;
	}
} /*}}}*/

void userChangedBrightness(int toLevel) { /*{{{*/
	if(verbose == 1 && daemonize == 0) {
		printf("User changed brightness level to %d\n", toLevel);
	}

	/* Reset alarm */
	alarm(waitSeconds);

	if(isDark == 1 && toLevel > darkBright) {
		/* Fade to max immediately */
		info("Maximum lightness, because user increased brightness");
		setBrightness(maxBright);
		savedLevel = maxBright;
		isDark = 0;
	}
	if(toLevel < darkBright) {
		/* Deny this */
		info("Sorry, you can't lower brightness below darkBright");
		setBrightness(darkBright);
		savedLevel = darkBright;
	}
	savedLevel = toLevel;
} /*}}}*/

/* }}} */

/*
 * Make pid file.
 */
void make_pidfile(char *pidfile) {
	FILE *fpidfile;
	if (pidfile && (fpidfile = fopen(pidfile, "w"))) {
		fprintf(fpidfile, "%d\n", getpid());
		fclose(fpidfile);
	}
}

/*
 * The main program
 */
int main(int argc, char *argv[]) { /*{{{*/
	int  defaultBrightness = 0;
	int  oldBrightness     = 0;
	int  newBrightness     = 0;
	int  highFd	       = 0;
	#ifdef X11
		int  noXCode	       = 0;
		int  inotifyFdX11;
	#endif
	int  eventReceived;
	int  inotifyFdEvents;
	int  i;
	int  fd;
	int  filesCount;
	char option;
	char user[25];
	char buf[255];
	char buf2[255];
	struct passwd *userStruct;
	glob_t *events;
	fd_set rfds;
	fd_set openfds;
	struct inotify_event *inotifyEvent;

	#ifdef DEBUG
		printf("DEBUG BUILD\n");
	#endif

	/* Load settings {{{ */
	strcpy(user, "nobody");
	*pidfile = 0;
	
	loadDefaultClass();
	opterr = 0;

	regcomp(&eventSourceFilter, ".*event.*", REG_EXTENDED | REG_NOSUB);
	#ifdef X11
	while((option = getopt(argc, argv, "vdfxe:w:P:b:c:r:u:")) > 0) {
	#else
	while((option = getopt(argc, argv, "vdfe:w:P:b:c:r:u:")) > 0) {
	#endif
		switch(option) {
			case 'v': /* Verbose */
				verbose = 1;
				break;
			case 'w': /* Wait n seconds before fading */
				waitSeconds = atoi(optarg);
				if(waitSeconds <= 0) {
					printHelp();
				}
				break;
			case 'd': /* Daemonize */
				daemonize = 1;
				break;
			case 'P': /* Location of pid file */
				if(strlen(optarg) > 250) {
					error("The filename should not be longer than 250 characters.");
				}
				strcpy(pidfile, optarg);
				break;
			case 'e': /* Event source filter */
				/* Compile regex */
				i = regcomp(&eventSourceFilter, optarg, REG_EXTENDED | REG_NOSUB);
				if(i != 0) {
					regerror(i, &eventSourceFilter, buf, 255);
					printf("Error: %s\n", buf);
					error("Regex compilation failed");
				}
				break;
			case 'b': /* Darkest setting */
				darkBright = atoi(optarg);
				if(darkBright < 0 || darkBright > 5) {
					printHelp();
				}
				break;
			case 'f': /* Force fading */
				force++;
				break;
			case 'u': /* User */
				if(strlen(optarg) > 20) {
					error("The user name should not be longer than 20 characters.");
				}
				strcpy(user, optarg);
				break;
			case 'c': /* Brightness class */
				if(strlen(optarg) > 20) {
					error("The class should not be longer than 20 characters.");
				}
				strcpy(b_class, optarg);
				break;
			#ifdef X11
			case 'x': /* Deactivate X-Code */
				noXCode = 1;
				break;
			#endif
			case 'r': /* Brightness FIFO */
				if(strlen(optarg) > 250) {
					error("The filename should not be longer than 250 characters.");
				}
				strcpy(b_fifo, optarg);
				bfifoFd = 1;
				break;
			default:
				printHelp();
		}
	}
	
	
	/* }}} */

	/* Load some sysfs and default settings stuff {{{ */
	/* Get the user ID */
	userStruct = getpwnam(user);
	if(userStruct == NULL) {
		error("Failed to get the UID for the specified user");
	}

	/* Set files in sysfs to use */
	sprintf(brightnessFile, "/sys/class/backlight/%s/brightness", b_class);
	brightnessFd = fopen(brightnessFile, "w");
	if(!brightnessFd) {
		printf("Failed to open %s for writing.\n", brightnessFile);
		error(	"- Do you have the correct ACPI extensions in your kernel?\n"
			"- Do you have permissions to write to that file?");
	}

	/* Load maximum brightness; abuse some not yet initialised variables to do so ;) */
	sprintf(actualBrightnessFile, "/sys/class/backlight/%s/max_brightness", b_class);
	maxBright = getBrightness();
	if(verbose == 1 && daemonize == 0) {
		printf("Maximum brightness is %d\n", maxBright);
	}
	sprintf(actualBrightnessFile, "/sys/class/backlight/%s/actual_brightness", b_class);

	/* Load brightness */
	defaultBrightness = newBrightness = oldBrightness = getBrightness();

	/* Open all event files */
	FD_ZERO(&openfds);
	events = (glob_t *)malloc(sizeof(glob_t));
	if(glob("/dev/input/by-path/*", 0, NULL, events) != 0) {
		printf("Failed to list event devices in /dev/input/by-path/\n");
		exit(1);
	}
	filesCount = events->gl_pathc;
	for(i=0; i<filesCount; i++) {
		if(!isEventFileValid(events->gl_pathv[i])) {
			continue;
		}
		fd = open(events->gl_pathv[i], O_RDONLY | O_NONBLOCK);
		#ifdef DEBUG
		printf("%d: %s\n", fd, events->gl_pathv[i]);
		#endif

		if(fd == -1) {
			printf("Failed to open %s\n", events->gl_pathv[i]);
			error("You have to be root to run this");
		}
		if(fd > highFd) {
			highFd = fd;
		}
		FD_SET(fd, &openfds);
	}
	globfree(events);

	/* Create inotify mapping for that directory */
	inotifyFdEvents = inotify_init();
	if(inotifyFdEvents != -1) {
		if(inotify_add_watch(inotifyFdEvents, "/dev/input/by-path/", IN_CREATE) == -1) {
			close(inotifyFdEvents);
			inotifyFdEvents = -1;
		}
		if(inotifyFdEvents > highFd) {
			highFd = inotifyFdEvents;
		}
	}

	/* Open FIFO */
	if(bfifoFd == 1) {
		info("Creating and opening FIFO");
		unlink(b_fifo);
		if(mkfifo(b_fifo, 0777) != 0) {
			error("Failed to create FIFO");
		}
		bfifoFd = open(b_fifo, O_RDONLY | O_NONBLOCK);
		#ifdef DEBUG
			printf("%d: Fifo\n", bfifoFd);
		#endif
		if(bfifoFd == -1) {
			error("Failed to open FIFO for reading");
		}
		if(bfifoFd > highFd) {
			highFd = bfifoFd;
		}
	}
	/* }}} */

	/* Signal handlers, daemonize and X-Server opening {{{ */
	/* Create signal handler for closing the display */
	signal(SIGINT,	signalHandlerQuit);
	signal(SIGHUP,	signalHandlerQuit);
	signal(SIGALRM, signalHandlerAlarm);


	/* Daemonize */
	#ifndef DEBUG
	if(daemonize == 1) {
		daemon(0, 0);
	}
	#endif

	/* Create PID file */
	if(*pidfile)
		make_pidfile(pidfile);

	#ifdef X11
	/* Wait for an X-Server to appear */
	if(noXCode == 0) {
		inotifyFdX11 = inotify_init();
		if(inotifyFdX11 != -1) {
			if(inotify_add_watch(inotifyFdX11, "/tmp/.X11-unix/", IN_CREATE) == -1) {
				close(inotifyFdX11);
				inotifyFdX11 = -1;
			}
		}
		while(1) {
			/* Try opening $DISPLAY first */
			if((display = XOpenDisplay(NULL)) != NULL) {
				info("Opened X11 Display from ENV{DISPLAY}");
			}
			else {
				/* Failed to open $DISPLAY - wait for X11 to appear */
				if(inotifyFdX11 != -1) {
					info("Waiting for an X-Server to appear(!)");

					read(inotifyFdX11, buf, sizeof(struct inotify_event) * 2);
					inotifyEvent = (struct inotify_event *)buf;
					strncpy(buf, inotifyEvent->name, 3);
					buf[0] = ':';
					buf[2] = 0;
					if(verbose == 1 && daemonize == 0) {
						printf("Found X-Server '%s'; I will try to open a connection to it every 5 seconds\n", buf);
					}
				}
				else
				{
					info("inotify on /tmp/.X11-unix failed - polling for ':0'");
					strcpy(buf, ":0");
				}

				display = XOpenDisplay(buf);
				while(display == NULL) {
					sleep(5);
					display = XOpenDisplay(buf);
				}
			}

			info("Opened a connection");
			if(fork() == 0) {
				break;
			}
			waitpid(0, NULL, 0);
			info("Connection lost.");
			sleep(5);
		}
	}
	#endif

	/* Drop privileges */
	setegid(userStruct->pw_gid);
	setgid(userStruct->pw_gid);
	seteuid(userStruct->pw_uid);
	setuid(userStruct->pw_uid);
	/* }}} */

	/* Wait for input */
	alarm(waitSeconds);
	while(1) {
		eventReceived = 0;
		rfds = openfds;

		/* Add inotify and FIFO to watch list */
		if(inotifyFdEvents != 0) {
			FD_SET(inotifyFdEvents, &rfds);
		}
		if(bfifoFd != 0) {
			FD_SET(bfifoFd, &rfds);
		}

		/* Select filepointers */
		while(select(highFd + 1, &rfds, NULL, NULL, NULL) == -1) {
			#ifdef DEBUG
			info("DEBUG: Select failed.");
			#endif
		}

		/* Check for event fd */
		for(fd=0; fd<sizeof(fd_set) * 8; fd++) {
			if(FD_ISSET(fd, &openfds) && FD_ISSET(fd, &rfds)) {
				#ifdef DEBUG
				printf("DEBUG: fd %d fired\n", fd);
				#endif

				if(read(fd, buf, 250) == -1) {
					#ifdef DEBUG
					printf("DEBUG: fd %d closed\n", fd);
					#endif
					close(fd);
					FD_CLR(fd, &openfds);
					if(fd == highFd) {
						i = fd;
						while(i > 0 && (!FD_ISSET(--i, &openfds)) && (i != bfifoFd));
						highFd = i + 1;
					}
				}
				else {
					eventReceived = 1;
				}
			}
		}
		
		/* Event received */
		if(eventReceived) {
			handleReceivedEvent();
		}

		/* FIFO action */
		if(bfifoFd != 0 && FD_ISSET(bfifoFd, &rfds)) {
			memset(buf, 0, 5);
			if(read(bfifoFd, buf, 2) != -1) {
				if(strlen(buf) > 0) {
					#ifdef DEBUG
					printf("Fifo said: %s\n", buf);
					#endif
					i = atoi(buf);
					if(savedLevel != i && lastFade + 1 < time(NULL)) {
						userChangedBrightness(i);
					}
				}
				else {
					/* Reopen FIFO */
					close(bfifoFd);
					bfifoFd = open(b_fifo, O_RDONLY | O_NONBLOCK);
					#ifdef DEBUG
						info("Reopening FIFO");
					#endif
					if(bfifoFd == -1) {
						error("Failed to open FIFO for reading");
					}
					if(bfifoFd > highFd) {
						highFd = bfifoFd;
					}
				}
			}
		}

		/* Inotify on /dev/input */
		if(inotifyFdEvents != -1 && FD_ISSET(inotifyFdEvents, &rfds)) {
			memset(buf, 0, sizeof(struct inotify_event) + 51);
			read(inotifyFdEvents, buf, sizeof(struct inotify_event) + 50);
			inotifyEvent = (struct inotify_event *)buf;
			if(inotifyEvent->len > 0 && strstr(inotifyEvent->name, "event") != NULL) {
				sprintf(buf2, "/dev/input/by-path/%s", inotifyEvent->name);
				if(!isEventFileValid(buf2)) {
					continue;
				}
				fd = open(buf2, O_RDONLY | O_NONBLOCK);
				#ifdef DEBUG
				printf("%d: %s\n", fd, buf2);
				#endif
				if(fd == -1) {
					if(verbose == 1 && daemonize == 0) {
						printf("Failed to open %s\n", buf2);
					}
				}
				else {
					if(fd > highFd) {
						highFd = fd;
					}
					FD_SET(fd, &openfds);
				}
			}
		}
	}
} /*}}}*/

/* vim:ft=c:fileencoding=iso-8859-1:foldmethod=marker:noexpandtab
 **/
