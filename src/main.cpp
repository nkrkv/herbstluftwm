/** Copyright 2011-2013 Thorsten Wißmann. All rights reserved.
 *
 * This software is licensed under the "Simplified BSD License".
 * See LICENSE for details */

// herbstluftwm
#include "root.h"
#include "clientmanager.h"
#include "utils.h"
#include "key.h"
#include "layout.h"
#include "globals.h"
#include "ipc-server.h"
#include "ipc-protocol.h"
#include "command.h"
#include "settings.h"
#include "hook.h"
#include "mouse.h"
#include "rules.h"
#include "ewmh.h"
#include "stack.h"
#include "object.h"
#include "decoration.h"
#include "xconnection.h"
// standard
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <sstream>
// gui
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>


// globals:
int g_verbose = 0;
Display*    g_display;
int         g_screen;
Window      g_root;
int         g_screen_width;
int         g_screen_height;
bool        g_aboutToQuit;

// module internals:
static Bool     g_otherwm;
static int (*g_xerrorxlib)(Display *, XErrorEvent *);
static char*    g_autostart_path = NULL; // if not set, then find it in $HOME or $XDG_CONFIG_HOME
static int*     g_focus_follows_mouse = NULL;
static bool     g_exec_before_quit = false;
static char**   g_exec_args = NULL;
static int*     g_raise_on_click = NULL;

typedef void (*HandlerTable[LASTEvent]) (XEvent*);

int quit();
int reload();
int version(int argc, char* argv[], Output output);
int echo(int argc, char* argv[], Output output);
int true_command();
int false_command();
int try_command(int argc, char* argv[], Output output);
int silent_command(int argc, char* argv[]);
int print_layout_command(int argc, char** argv, Output output);
int load_command(int argc, char** argv, Output output);
int print_tag_status_command(int argc, char** argv, Output output);
void execute_autostart_file();
int raise_command(int argc, char** argv, Output output);
int spawn(int argc, char** argv);
int wmexec(int argc, char** argv);
static void remove_zombies(int signal);
int custom_hook_emit(int argc, const char** argv);
int jumpto_command(int argc, char** argv, Output output);
int getenv_command(int argc, char** argv, Output output);
int setenv_command(int argc, char** argv, Output output);
int unsetenv_command(int argc, char** argv, Output output);

// handler for X-Events
void buttonpress(XEvent* event);
void buttonrelease(XEvent* event);
void createnotify(XEvent* event);
void configurerequest(XEvent* event);
void configurenotify(XEvent* event);
void destroynotify(XEvent* event);
void enternotify(XEvent* event);
void expose(XEvent* event);
void focusin(XEvent* event);
void keypress(XEvent* event);
void mappingnotify(XEvent* event);
void motionnotify(XEvent* event);
void mapnotify(XEvent* event);
void maprequest(XEvent* event);
void propertynotify(XEvent* event);
void unmapnotify(XEvent* event);

unique_ptr<CommandTable> commands() {
    return unique_ptr<CommandTable>(new CommandTable{
        {"quit",           quit},
        {"echo",           echo},
        {"true",           true_command},
        {"false",          false_command},
        {"try",            try_command},
        {"silent",         silent_command},
        {"reload",         reload},
        {"version",        version},
        {"list_commands",  list_commands},
        {"list_monitors",  list_monitors},
        {"set_monitors",   set_monitor_rects_command},
        {"disjoin_rects",  disjoin_rects_command},
        {"list_keybinds",  key_list_binds},
        {"list_padding",   list_padding},
        {"keybind",        keybind},
        {"keyunbind",      keyunbind},
        {"mousebind",      mouse_bind_command},
        {"mouseunbind",    mouse_unbind_all},
        {"spawn",          spawn},
        {"wmexec",         wmexec},
        {"emit_hook",      custom_hook_emit},
        {"bring",          frame_current_bring},
        {"focus_nth",      frame_current_set_selection},
        {"cycle",          frame_current_cycle_selection},
        {"cycle_all",      cycle_all_command},
        {"cycle_layout",   frame_current_cycle_client_layout},
        {"cycle_frame",    cycle_frame_command},
        {"close",          close_command},
        {"close_or_remove",close_or_remove_command},
        {"close_and_remove",close_and_remove_command},
        {"split",          frame_split_command},
        {"resize",         frame_change_fraction_command},
        {"focus_edge",     frame_focus_edge},
        {"focus",          frame_focus_command},
        {"shift_edge",     frame_move_window_edge},
        {"shift",          frame_move_window_command},
        {"shift_to_monitor",shift_to_monitor},
        {"remove",         frame_remove_command},
        {"set",            settings_set_command},
        {"toggle",         settings_toggle},
        {"cycle_value",    settings_cycle_value},
        {"cycle_monitor",  monitor_cycle_command},
        {"focus_monitor",  monitor_focus_command},
        {"get",            settings_get},
        {"add",            tag_add_command},
        {"use",            monitor_set_tag_command},
        {"use_index",      monitor_set_tag_by_index_command},
        {"use_previous",   monitor_set_previous_tag_command},
        {"jumpto",         jumpto_command},
        {"floating",       tag_set_floating_command},
        {"fullscreen",     client_set_property_command},
        {"pseudotile",     client_set_property_command},
        {"tag_status",     print_tag_status_command},
        {"merge_tag",      tag_remove_command},
        {"rename",         tag_rename_command},
        {"move",           tag_move_window_command},
        {"rotate",         layout_rotate_command},
        {"move_index",     tag_move_window_by_index_command},
        {"add_monitor",    add_monitor_command},
        {"raise_monitor",  monitor_raise_command},
        {"remove_monitor", remove_monitor_command},
        {"move_monitor",   move_monitor_command},
        {"rename_monitor", rename_monitor_command},
        {"monitor_rect",   monitor_rect_command},
        {"pad",            monitor_set_pad_command},
        {"raise",          raise_command},
        {"rule",           rule_add_command},
        {"unrule",         rule_remove_command},
        {"list_rules",     rule_print_all_command},
        {"layout",         print_layout_command},
        {"stack",          print_stack_command},
        {"dump",           print_layout_command},
        {"load",           load_command},
        {"complete",       complete_command},
        {"complete_shell", complete_command},
        {"lock",           monitors_lock_command},
        {"unlock",         monitors_unlock_command},
        {"lock_tag",       monitor_lock_tag_command},
        {"unlock_tag",     monitor_unlock_tag_command},
        {"set_layout",     frame_current_set_client_layout},
        {"detect_monitors",detect_monitors_command},
        {"chain",          command_chain_command},
        {"and",            command_chain_command},
        {"or",             command_chain_command},
        {"!",              negate_command},
        // {"attr",           attr_command},
        // {"compare",        compare_command},
        {"object_tree",    print_object_tree_command},
        // {"get_attr",       hsattribute_get_command},
        // {"set_attr",       hsattribute_set_command},
        // {"new_attr",       userattribute_command},
        // {"mktemp",         tmpattribute_command},
        // {"remove_attr",    userattribute_remove_command},
        // {"substitute",     substitute_command},
        // {"sprintf",        sprintf_command},
        {"getenv",         getenv_command},
        {"setenv",         setenv_command},
        {"unsetenv",       unsetenv_command},
        {"ls",             Root::cmd_ls},
        {"get_attr",       Root::cmd_get_attr},
        {"attr",           Root::cmd_attr},
    });
}

// core functions
int quit() {
    g_aboutToQuit = true;
    return 0;
}

// reload config
int reload() {
    execute_autostart_file();
    return 0;
}

int version(int argc, char* argv[], Output output) {
    (void) argc;
    (void) argv;
    output << HERBSTLUFT_VERSION_STRING;
    return 0;
}

int echo(int argc, char* argv[], Output output) {
    if (SHIFT(argc, argv)) {
        // if there still is an argument
        output << argv[0];
        while (SHIFT(argc, argv)) {
            output << " " << argv[0];
        }
    }
    output << '\n';
    return 0;
}

int true_command() {
    return 0;
}

int false_command() {
    return 1;
}

int try_command(int argc, char* argv[], Output output) {
    if (argc <= 1) {
        return HERBST_NEED_MORE_ARGS;
    }
    (void)SHIFT(argc, argv);
    call_command(argc, argv, output);
    return 0;
}

int silent_command(int argc, char* argv[]) {
    if (argc <= 1) {
        return HERBST_NEED_MORE_ARGS;
    }
    (void)SHIFT(argc, argv);
    return call_command_no_output(argc, argv);
}

// prints or dumps the layout of an given tag
// first argument tells whether to print or to dump
int print_layout_command(int argc, char** argv, Output output) {
    HSTag* tag = NULL;
    // an empty argv[1] means current focused tag
    if (argc >= 2 && argv[1][0] != '\0') {
        tag = find_tag(argv[1]);
        if (!tag) {
            output << argv[0] << ": Tag \"" << argv[1] << "\" not found\n";
            return HERBST_INVALID_ARGUMENT;
        }
    } else { // use current tag
        HSMonitor* m = get_current_monitor();
        tag = m->tag;
    }
    assert(tag != NULL);

    std::shared_ptr<HSFrame> frame = tag->frame->lookup(argc >= 3 ? argv[2] : "");
    if (argc > 0 && !strcmp(argv[0], "dump")) {
        frame->dump(output);
    } else {
        print_frame_tree(frame, output);
    }
    return 0;
}

int load_command(int argc, char** argv, Output output) {
    // usage: load TAG LAYOUT
    HSTag* tag = NULL;
    if (argc < 2) {
        return HERBST_NEED_MORE_ARGS;
    }
    char* layout_string = argv[1];
    if (argc >= 3) {
        tag = find_tag(argv[1]);
        layout_string = argv[2];
        if (!tag) {
            output << argv[0] << ": Tag \"" << argv[1] << "\" not found\n";
            return HERBST_INVALID_ARGUMENT;
        }
    } else { // use current tag
        HSMonitor* m = get_current_monitor();
        tag = m->tag;
    }
    assert(tag != NULL);
    char* rest = load_frame_tree(tag->frame, layout_string, output);
    tag_set_flags_dirty(); // we probably changed some window positions
    // arrange monitor
    HSMonitor* m = find_monitor_with_tag(tag);
    if (m) {
        tag->frame->setVisibleRecursive(true);
        if (get_current_monitor() == m) {
            frame_focus_recursive(tag->frame);
        }
        monitor_apply_layout(m);
    } else {
        tag->frame->setVisibleRecursive(false);
    }
    if (!rest) {
        output << argv[0] << ": Error while parsing!\n";
        return HERBST_INVALID_ARGUMENT;
    }
    if (rest[0] != '\0') { // if string was not parsed completely
        output << argv[0] << ": Layout description was too long\n";
        output << argv[0] << ": \"" << rest << "\" has not been parsed\n";
        return HERBST_INVALID_ARGUMENT;
    }
    return 0;
}

int print_tag_status_command(int argc, char** argv, Output output) {
    HSMonitor* monitor;
    if (argc >= 2) {
        monitor = string_to_monitor(argv[1]);
    } else {
        monitor = get_current_monitor();
    }
    if (monitor == NULL) {
        output << argv[0] << ": Monitor \"" << argv[1] << "\" not found!\n";
        return HERBST_INVALID_ARGUMENT;
    }
    tag_update_flags();
    output << '\t';
    for (int i = 0; i < tag_get_count(); i++) {
        HSTag* tag = get_tag_by_index(i);
        // print flags
        char c = '.';
        if (tag->flags & TAG_FLAG_USED) {
            c = ':';
        }
        HSMonitor *tag_monitor = find_monitor_with_tag(tag);
        if (tag_monitor == monitor) {
            c = '+';
            if (monitor == get_current_monitor()) {
                c = '#';
            }
        } else if (tag_monitor) {
            c = '-';
            if (get_current_monitor() == tag_monitor) {
                c = '%';
            }
        }
        if (tag->flags & TAG_FLAG_URGENT) {
            c = '!';
        }
        output << c;
        output << *tag->name;
        output << '\t';
    }
    return 0;
}

int custom_hook_emit(int argc, const char** argv) {
    hook_emit(argc - 1, argv + 1);
    return 0;
}

// spawn() heavily inspired by dwm.c
int spawn(int argc, char** argv) {
    if (argc < 2) {
        return HERBST_NEED_MORE_ARGS;
    }
    if (fork() == 0) {
        // only look in child
        if (g_display) {
            close(ConnectionNumber(g_display));
        }
        // shift all args in argv by 1 to the front
        // so that we have space for a NULL entry at the end for execvp
        char** execargs = argv_duplicate(argc, argv);
        free(execargs[0]);
        int i;
        for (i = 0; i < argc-1; i++) {
            execargs[i] = execargs[i+1];
        }
        execargs[i] = NULL;
        // do actual exec
        setsid();
        execvp(execargs[0], execargs);
        fprintf(stderr, "herbstluftwm: execvp \"%s\"", argv[1]);
        perror(" failed");
        exit(0);
    }
    return 0;
}

int wmexec(int argc, char** argv) {
    if (argc >= 2) {
        // shift all args in argv by 1 to the front
        // so that we have space for a NULL entry at the end for execvp
        char** execargs = argv_duplicate(argc, argv);
        free(execargs[0]);
        int i;
        for (i = 0; i < argc-1; i++) {
            execargs[i] = execargs[i+1];
        }
        execargs[i] = NULL;
        // quit and exec to new window manger
        g_exec_args = execargs;
    } else {
        // exec into same command
        g_exec_args = NULL;
    }
    g_exec_before_quit = true;
    g_aboutToQuit = true;
    return EXIT_SUCCESS;
}

int raise_command(int argc, char** argv, Output output) {
    auto client = get_client((argc > 1) ? argv[1] : "");
    if (client) {
        client->raise();
    } else {
        auto window = get_window((argc > 1) ? argv[1] : "");
        if (window)
            XRaiseWindow(g_display, std::stoul(argv[1], nullptr, 0));
        else return HERBST_INVALID_ARGUMENT;
    }
    return 0;
}

int jumpto_command(int argc, char** argv, Output output) {
    if (argc < 2) {
        return HERBST_NEED_MORE_ARGS;
    }
    auto client = get_client(argv[1]);
    if (client) {
        focus_client(client, true, true);
        return 0;
    } else {
        output << argv[0] << ": Could not find client";
        if (argc > 1) {
            output << " \"" << argv[1] << "\".\n";
        } else {
            output << ".\n";
        }
        return HERBST_INVALID_ARGUMENT;
    }
}

int getenv_command(int argc, char** argv, Output output) {
    if (argc < 2) {
        return HERBST_NEED_MORE_ARGS;
    }
    char* envvar = getenv(argv[1]);
    if (envvar == NULL) {
        return HERBST_ENV_UNSET;
    }
    output << envvar << "\n";
    return 0;
}

int setenv_command(int argc, char** argv, Output output) {
    if (argc < 3) {
        return HERBST_NEED_MORE_ARGS;
    }
    if (setenv(argv[1], argv[2], 1) != 0) {
        output << argv[0] << ": Could not set environment variable: " << strerror(errno) << "\n";
        return HERBST_UNKNOWN_ERROR;
    }
    return 0;
}

int unsetenv_command(int argc, char** argv, Output output) {
    if (argc < 2) {
        return HERBST_NEED_MORE_ARGS;
    }
    if (unsetenv(argv[1]) != 0) {
        output << argv[0] << ": Could not unset environment variable: " << strerror(errno) << "\n";
        return HERBST_UNKNOWN_ERROR;
    }
    return 0;
}

// handle x-events:

void event_on_configure(XEvent event) {
    XConfigureRequestEvent* cre = &event.xconfigurerequest;
    HSClient* client = get_client_from_window(cre->window);
    if (client) {
        bool changes = false;
        auto newRect = client->float_size_;
        if (client->sizehints_floating_ &&
            (client->is_client_floated() || client->pseudotile_))
        {
            bool width_requested = 0 != (cre->value_mask & CWWidth);
            bool height_requested = 0 != (cre->value_mask & CWHeight);
            bool x_requested = 0 != (cre->value_mask & CWX);
            bool y_requested = 0 != (cre->value_mask & CWY);
            cre->width += 2*cre->border_width;
            cre->height += 2*cre->border_width;
            if (width_requested && newRect.width  != cre->width) changes = true;
            if (height_requested && newRect.height != cre->height) changes = true;
            if (x_requested || y_requested) changes = true;
            if (x_requested) newRect.x = cre->x;
            if (y_requested) newRect.y = cre->y;
            if (width_requested) newRect.width = cre->width;
            if (height_requested) newRect.height = cre->height;
        }
        if (changes && client->is_client_floated()) {
            client->float_size_ = newRect;
            client->resize_floating(find_monitor_with_tag(client->tag()), client == get_current_client());
        } else if (changes && client->pseudotile_) {
            client->float_size_ = newRect;
            monitor_apply_layout(find_monitor_with_tag(client->tag()));
        } else {
        // FIXME: why send event and not XConfigureWindow or XMoveResizeWindow??
            client->send_configure();
        }
    } else {
        // if client not known.. then allow configure.
        // its probably a nice conky or dzen2 bar :)
        XWindowChanges wc;
        wc.x = cre->x;
        wc.y = cre->y;
        wc.width = cre->width;
        wc.height = cre->height;
        wc.border_width = cre->border_width;
        wc.sibling = cre->above;
        wc.stack_mode = cre->detail;
        XConfigureWindow(g_display, cre->window, cre->value_mask, &wc);
    }
}

// from dwm.c
/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's).  Other types of errors call Xlibs
 * default error handler, which may call exit.  */
int xerror(Display *dpy, XErrorEvent *ee) {
    if(ee->error_code == BadWindow
    || ee->error_code == BadGC
    || ee->error_code == BadPixmap
    || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
    || (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
    || (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
    || (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
    || (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
    || (ee->request_code == X_CopyArea && ee->error_code == BadDrawable)) {
        return 0;
    }
    fprintf(stderr, "herbstluftwm: fatal error: request code=%d, error code=%d\n",
            ee->request_code, ee->error_code);
    if (ee->error_code == BadDrawable) {
        HSDebug("Warning: ignoring X_BadDrawable");
        return 0;
    }
    return g_xerrorxlib(dpy, ee); /* may call exit */
}

int xerrordummy(Display *dpy, XErrorEvent *ee) {
    return 0;
}

// from dwm.c
/* Startup Error handler to check if another window manager
 * is already running. */
int xerrorstart(Display *dpy, XErrorEvent *ee) {
    g_otherwm = True;
    return -1;
}

// from dwm.c
void checkotherwm(void) {
    g_otherwm = False;
    g_xerrorxlib = XSetErrorHandler(xerrorstart);
    /* this causes an error if some other window manager is running */
    XSelectInput(g_display, DefaultRootWindow(g_display), SubstructureRedirectMask);
    XSync(g_display, False);
    if(g_otherwm)
        die("herbstluftwm: another window manager is already running\n");
    XSetErrorHandler(xerror);
    XSync(g_display, False);
}

// scan for windows and add them to the list of managed clients
// from dwm.c
void scan(void) {
    unsigned int num;
    Window d1, d2, *cl, *wins = NULL;
    unsigned long cl_count;
    XWindowAttributes wa;

    ewmh_get_original_client_list(&cl, &cl_count);
    if (XQueryTree(g_display, g_root, &d1, &d2, &wins, &num)) {
        for (int i = 0; i < num; i++) {
            if(!XGetWindowAttributes(g_display, wins[i], &wa)
            || wa.override_redirect || XGetTransientForHint(g_display, wins[i], &d1))
                continue;
            // only manage mapped windows.. no strange wins like:
            //      luakit/dbus/(ncurses-)vim
            // but manage it if it was in the ewmh property _NET_CLIENT_LIST by
            // the previous window manager
            // TODO: what would dwm do?
            if (is_window_mapped(g_display, wins[i])
                || 0 <= array_find(cl, cl_count, sizeof(Window), wins+i)) {
                manage_client(wins[i], true);
                XMapWindow(g_display, wins[i]);
            }
        }
        if(wins)
            XFree(wins);
    }
    // ensure every original client is managed again
    for (int i = 0; i < cl_count; i++) {
        if (get_client_from_window(cl[i])) continue;
        if (!XGetWindowAttributes(g_display, cl[i], &wa)
            || wa.override_redirect
            || XGetTransientForHint(g_display, cl[i], &d1))
        {
            continue;
        }
        XReparentWindow(g_display, cl[i], g_root, 0,0);
        manage_client(cl[i], true);
    }
}

void execute_autostart_file() {
    GString* path = NULL;
    if (g_autostart_path) {
        path = g_string_new(g_autostart_path);
    } else {
        // find right directory
        char* xdg_config_home = getenv("XDG_CONFIG_HOME");
        if (xdg_config_home) {
            path = g_string_new(xdg_config_home);
        } else {
            char* home = getenv("HOME");
            if (!home) {
                g_warning("Will not run autostart file. "
                          "Neither $HOME or $XDG_CONFIG_HOME is set.\n");
                return;
            }
            path = g_string_new(home);
            g_string_append_c(path, G_DIR_SEPARATOR);
            g_string_append(path, ".config");
        }
        g_string_append_c(path, G_DIR_SEPARATOR);
        g_string_append(path, HERBSTLUFT_AUTOSTART);
    }
    if (0 == fork()) {
        if (g_display) {
            close(ConnectionNumber(g_display));
        }
        setsid();
        execl(path->str, path->str, NULL);

        const char* global_autostart = HERBSTLUFT_GLOBAL_AUTOSTART;
        HSDebug("Can not execute %s, falling back to %s\n", path->str, global_autostart);
        execl(global_autostart, global_autostart, NULL);

        fprintf(stderr, "herbstluftwm: execvp \"%s\"", global_autostart);
        perror(" failed");
        exit(EXIT_FAILURE);
    }
    g_string_free(path, true);
}

static void parse_arguments(int argc, char** argv) {
    static struct option long_options[] = {
        {"autostart",   1, 0, 'c'},
        {"version",     0, 0, 'v'},
        {"locked",      0, 0, 'l'},
        {"verbose",     0, &g_verbose, 1},
        {0, 0, 0, 0}
    };
    // parse options
    while (1) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "+c:vl", long_options, &option_index);
        if (c == -1) break;
        switch (c) {
            case 0:
                /* ignore recognized long option */
                break;
            case 'v':
                printf("%s %s\n", argv[0], HERBSTLUFT_VERSION);
                printf("Copyright (c) 2011-2014 Thorsten Wißmann\n");
                printf("Released under the Simplified BSD License\n");
                exit(0);
                break;
            case 'c':
                g_autostart_path = optarg;
                break;
            case 'l':
                g_initial_monitors_locked = 1;
                break;
            default:
                exit(EXIT_FAILURE);
        }
    }
}

static void remove_zombies(int signal) {
    int bgstatus;
    while (waitpid(-1, &bgstatus, WNOHANG) > 0);
}

static void handle_signal(int signal) {
    HSDebug("Interrupted by signal %d\n", signal);
    g_aboutToQuit = true;
    return;
}

static void sigaction_signal(int signum, void (*handler)(int)) {
    struct sigaction act;
    act.sa_handler = handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    sigaction(signum, &act, NULL);
}

static void fetch_settings() {
    // fetch settings only for this main.c file from settings table
    g_focus_follows_mouse = &(settings_find("focus_follows_mouse")->value.i);
    g_raise_on_click = &(settings_find("raise_on_click")->value.i);
}

HandlerTable g_default_handler;

static void init_handler_table() {
    g_default_handler[ ButtonPress       ] = buttonpress;
    g_default_handler[ ButtonRelease     ] = buttonrelease;
    g_default_handler[ ClientMessage     ] = ewmh_handle_client_message;
    g_default_handler[ ConfigureNotify   ] = configurenotify;
    g_default_handler[ ConfigureRequest  ] = configurerequest;
    g_default_handler[ CreateNotify      ] = createnotify;
    g_default_handler[ DestroyNotify     ] = destroynotify;
    g_default_handler[ EnterNotify       ] = enternotify;
    g_default_handler[ Expose            ] = expose;
    g_default_handler[ FocusIn           ] = focusin;
    g_default_handler[ KeyPress          ] = keypress;
    g_default_handler[ MapNotify         ] = mapnotify;
    g_default_handler[ MapRequest        ] = maprequest;
    g_default_handler[ MappingNotify     ] = mappingnotify;
    g_default_handler[ MotionNotify      ] = motionnotify;
    g_default_handler[ PropertyNotify    ] = propertynotify;
    g_default_handler[ UnmapNotify       ] = unmapnotify;
}

static struct {
    void (*init)();
    void (*destroy)();
} g_modules[] = {
    { ipc_init,         ipc_destroy         },
    { key_init,         key_destroy         },
    { settings_init,    settings_destroy    },
    { floating_init,    floating_destroy    },
    { stacklist_init,   stacklist_destroy   },
    { layout_init,      layout_destroy      },
    { tag_init,         tag_destroy         },
    { clientlist_init,  clientlist_destroy  },
    { decorations_init, decorations_destroy },
    { monitor_init,     monitor_destroy     },
    { ewmh_init,        ewmh_destroy        },
    { mouse_init,       mouse_destroy       },
    { hook_init,        hook_destroy        },
    { rules_init,       rules_destroy       },
};

/* ----------------------------- */
/* event handler implementations */
/* ----------------------------- */

void buttonpress(XEvent* event) {
    XButtonEvent* be = &(event->xbutton);
    HSDebug("name is: ButtonPress on sub %lx, win %lx\n", be->subwindow, be->window);
    if (mouse_binding_find(be->state, be->button)) {
        mouse_handle_event(event);
    } else {
        HSClient* client = get_client_from_window(be->window);
        if (client) {
            focus_client(client, false, true);
            if (*g_raise_on_click) {
                    client->raise();
            }
        }
    }
    XAllowEvents(g_display, ReplayPointer, be->time);
}

void buttonrelease(XEvent* event) {
    HSDebug("name is: ButtonRelease\n");
    mouse_stop_drag();
}

void createnotify(XEvent* event) {
    // printf("name is: CreateNotify\n");
    if (is_ipc_connectable(event->xcreatewindow.window)) {
        ipc_add_connection(event->xcreatewindow.window);
    }
}

void configurerequest(XEvent* event) {
    HSDebug("name is: ConfigureRequest\n");
    event_on_configure(*event);
}

void configurenotify(XEvent* event) {
    if (event->xconfigure.window == g_root &&
        settings_find("auto_detect_monitors")->value.i) {
        const char* args[] = { "detect_monitors" };
        std::ostringstream void_output;
        detect_monitors_command(LENGTH(args), args, void_output);
    }
    // HSDebug("name is: ConfigureNotify\n");
}

void destroynotify(XEvent* event) {
    // try to unmanage it
    //HSDebug("name is: DestroyNotify for %lx\n", event->xdestroywindow.window);
    auto cm = Root::get()->clients();
    auto client = cm->client(event->xunmap.window);
    if (client) cm->force_unmanage(client);
}

void enternotify(XEvent* event) {
    XCrossingEvent *ce = &event->xcrossing;
    //HSDebug("name is: EnterNotify, focus = %d\n", event->xcrossing.focus);
    if (!mouse_is_dragging()
        && *g_focus_follows_mouse
        && ce->focus == false) {
        HSClient* c = get_client_from_window(ce->window);
        std::shared_ptr<HSFrameLeaf> target;
        if (c && c->tag()->floating == false
              && (target = c->tag()->frame->frameWithClient(c))
              && target->getLayout() == LAYOUT_MAX
              && target->focusedClient() != c) {
            // don't allow focus_follows_mouse if another window would be
            // hidden during that focus change (which only occurs in max layout)
        } else if (c) {
            focus_client(c, false, true);
        }
    }
}

void expose(XEvent* event) {
    //if (event->xexpose.count > 0) return;
    //Window ewin = event->xexpose.window;
    //HSDebug("name is: Expose for window %lx\n", ewin);
}

void focusin(XEvent* event) {
    //HSDebug("name is: FocusIn\n");
}

void keypress(XEvent* event) {
    //HSDebug("name is: KeyPress\n");
    handle_key_press(event);
}

void mappingnotify(XEvent* event) {
    {
        // regrab when keyboard map changes
        XMappingEvent *ev = &event->xmapping;
        XRefreshKeyboardMapping(ev);
        if(ev->request == MappingKeyboard) {
            regrab_keys();
            //TODO: mouse_regrab_all();
        }
    }
}

void motionnotify(XEvent* event) {
    handle_motion_event(event);
}

void mapnotify(XEvent* event) {
    //HSDebug("name is: MapNotify\n");
    HSClient* c;
    if ((c = get_client_from_window(event->xmap.window))) {
        // reset focus. so a new window gets the focus if it shall have the
        // input focus
        // TODO: reset input focus
        //frame_focus_recursive(get_current_monitor()->tag->frame->getFocusedFrame());
        // also update the window title - just to be sure
        c->update_title();
    }
}

void maprequest(XEvent* event) {
    HSDebug("name is: MapRequest\n");
    XMapRequestEvent* mapreq = &event->xmaprequest;
    if (is_herbstluft_window(g_display, mapreq->window)) {
        // just map the window if it wants that
        XWindowAttributes wa;
        if (!XGetWindowAttributes(g_display, mapreq->window, &wa)) {
            return;
        }
        XMapWindow(g_display, mapreq->window);
    } else if (!get_client_from_window(mapreq->window)) {
        // client should be managed (is not ignored)
        // but is not managed yet
        auto client = manage_client(mapreq->window, false);
        if (client && find_monitor_with_tag(client->tag())) {
            XMapWindow(g_display, mapreq->window);
        }
    }
    // else: ignore all other maprequests from windows
    // that are managed already
}

void propertynotify(XEvent* event) {
    // printf("name is: PropertyNotify\n");
    XPropertyEvent *ev = &event->xproperty;
    HSClient* client;
    if (ev->state == PropertyNewValue) {
        if (is_ipc_connectable(event->xproperty.window)) {
            ipc_handle_connection(event->xproperty.window);
        } else if((client = get_client_from_window(ev->window))) {
            if (ev->atom == XA_WM_HINTS) {
                client->update_wm_hints();
            } else if (ev->atom == XA_WM_NORMAL_HINTS) {
                client->updatesizehints();
                HSMonitor* m = find_monitor_with_tag(client->tag());
                if (m) monitor_apply_layout(m);
            } else if (ev->atom == XA_WM_NAME ||
                       ev->atom == g_netatom[NetWmName]) {
                client->update_title();
            }
        }
    }
}

void unmapnotify(XEvent* event) {
    HSDebug("name is: UnmapNotify for %lx\n", event->xunmap.window);
    Root::get()->clients()->unmap_notify(event->xunmap.window);
}

/* ---- */
/* main */
/* ---- */

#include "testobject.h"

int main(int argc, char* argv[]) {
    parse_arguments(argc, argv);
    g_display = XOpenDisplay(NULL);
    if (!g_display) {
        die("herbstluftwm: cannot open display\n");
    }
    checkotherwm();
    // remove zombies on SIGCHLD
    sigaction_signal(SIGCHLD, remove_zombies);
    sigaction_signal(SIGINT,  handle_signal);
    sigaction_signal(SIGQUIT, handle_signal);
    sigaction_signal(SIGTERM, handle_signal);
    // set some globals
    XConnection X = XConnection::connect();
    g_screen = X.screen();
    g_screen_width = X.screenWidth();
    g_screen_height = X.screenHeight();
    g_root = X.root();
    XSelectInput(g_display, g_root, ROOT_EVENT_MASK);

    auto root = std::make_shared<Root>();
    Root::setRoot(root);
    //test_object_system();

    init_handler_table();
    Commands::initialize(commands());


    // initialize subsystems
    for (int i = 0; i < LENGTH(g_modules); i++) {
        g_modules[i].init();
    }
    fetch_settings();

    // setup
    ensure_monitors_are_available();
    scan();
    tag_force_update_flags();
    all_monitors_apply_layout();
    ewmh_update_all();
    execute_autostart_file();

    // main loop
    XEvent event;
    int x11_fd;
    fd_set in_fds;
    x11_fd = ConnectionNumber(g_display);
    while (!g_aboutToQuit) {
        FD_ZERO(&in_fds);
        FD_SET(x11_fd, &in_fds);
        // wait for an event or a signal
        select(x11_fd + 1, &in_fds, 0, 0, NULL);
        if (g_aboutToQuit) {
            break;
        }
        while (XPending(g_display)) {
            XNextEvent(g_display, &event);
            void (*handler) (XEvent*) = g_default_handler[event.type];
            if (handler != NULL) {
                handler(&event);
            }
        }
    }

    // destroy all subsystems
    for (int i = LENGTH(g_modules); i --> 0;) {
        g_modules[i].destroy();
    }
    root.reset();
    // check if we shall restart an other window manager
    if (g_exec_before_quit) {
        if (g_exec_args) {
            // do actual exec
            HSDebug("==> Doing wmexec to %s\n", g_exec_args[0]);
            execvp(g_exec_args[0], g_exec_args);
            fprintf(stderr, "herbstluftwm: execvp \"%s\"", g_exec_args[0]);
            perror(" failed");
        }
        // on failure or if no other wm given, then fall back
        HSDebug("==> Doing wmexec to %s\n", argv[0]);
        execvp(argv[0], argv);
        fprintf(stderr, "herbstluftwm: execvp \"%s\"", argv[1]);
        perror(" failed");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

