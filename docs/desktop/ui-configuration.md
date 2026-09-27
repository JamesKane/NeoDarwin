<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- Provenance: distilled from the plan-neo pilot (/Users/jkane/Development/c/plan-neo, commit b0a35a1, 2026-09-22), where this design was first written and its reference implementation built and tested. This is NeoDarwin's normative text. -->

# Configuring the interface: themes with classes, and keys that bind

Two halves of one idea, which is MUI's idea: **the person using the machine
decides what it looks like and how it answers, and the application does not get
the last word.** On the Amiga that meant every widget was an object of a class,
every class had configurable frames, spacing, fonts and backgrounds, and the
user's preferences overrode the program's. This document takes libstyle there,
and adds the half MUI never had — a binding system with the reach of
Hyprland's, on a desktop that is not a tiling manager and is not going to
become one.

## 1. Where version 1 stops

**The theme is flat and fixed.** `docs/theme-engine.md` describes
`SECTION.KEY VALUE`, thirty named palette entries, six chrome dimensions, four
font slots. Unknown keys are errors — which is right, and which is also the
problem: there is no way to say "buttons in this application", or "this frame
but thinner", or anything an application's author did not anticipate. One
theme, globally, for the window server's own chrome. A client drawing its own
interface cannot ask the theme what a button should look like, so `wb`, `term`
and `edit` will each invent their own, and the desktop will look like three
programs.

**There are no keybindings.** In the pilot, `inputkey` (`wsys/ev.c:449`) routed every key
to the focused window. The server intercepts nothing, so there is no way to
close a window, switch a workspace, move a window or start a program from the
keyboard. Everything is the mouse.

On NeoDarwin `DK_META` — the Amiga key, the natural modifier for a window manager — is produced by `inputd` from the Super key.

## 2. Themes, version 2: classes and selectors

### 2.1 Everything drawn is an object of a class

The class list is the vocabulary a theme can talk about. It covers what the
server draws and what a client draws, because they are the same drawing:

```
desktop     grid, background, the drag plate
window      the frame as a whole
title       the title bar
gadget      close, depth, zoom, size, drag — the parts of a frame
menubar     the bar
menu        a dropped menu
item        one entry in a menu
icon        a hidden window or a file in wb
label       the text under an icon
button      a client's push button
field       a client's text field
list        a client's list, and its rows
scroll      a scrollbar, its trough and its thumb
tip         a tooltip
```

The last six exist so that a client can draw a themed interface without
inventing one, which is the difference between a desktop and a collection of
programs that share a screen.

### 2.2 Selectors, and the flat form that keeps round-tripping

The parser stays line-oriented and byte-identical through format-parse-format,
because `/n/desktop/theme` is a file you edit with `echo`. What changes is
that a key may carry a scope:

```
[app/][class][:state][.part].key   value
```

```
# the base: exactly the version 1 syntax, still valid
palette.frame            #101018
frame.border             4

# a class
button.frame             bevel
button.pad               6 3
button:hover.face        @gadget.hover
button:active.face       @gadget.active

# a part of a class
gadget.close.glyph       x
scroll.thumb.face        @edge

# one application's classes
term/button.pad          4 2
term/window.frame.border 2
edit/list.rowheight      20

# a state and an app together
wb/icon:selected.face    @hilite
```

Specificity decides, and it is counted rather than guessed: an app scope is
worth more than a class, a class more than a part, a part more than a state,
and an exact tie goes to the line that came later. `style_explain` (new) takes
a class and a key and prints which line won and which lost, because a cascade
nobody can interrogate is a cascade people work around.

### 2.3 The user always wins

Three files, read in order, later overriding earlier:

```
/System/Library/Style/default            the system theme
/System/Library/Style/app/NAME           what an application shipped for itself
~/lib/style                   the person's own
```

An application may state a preference; it may not enforce one. That is MUI's
rule and it is worth keeping literally: there is no mechanism in this design
for a program to mark a setting as its own. The only thing an application gets
that the user does not is the right to be asked first.

### 2.4 Frames, backgrounds and metrics as named resources

MUI's frames were the thing people noticed, so they are first-class here:

```
frame bevel  { outset 1; light @edge; dark @edge.dark; pad 2 }
frame inset  { inset  1; light @edge.dark; dark @edge; pad 2 }
frame thin   { line 1; colour @edge; pad 1 }
frame none   { pad 0 }
frame neon   { line 1; colour @edge; glow 2 @edge.glow; pad 3 }

background plate   { flat @frame }
background grid    { dots 8 @grid on @desktop }
background fade    { vertical @title.from @title.to bands 8 dither }
background art     { image /System/Library/Style/art/plate.bit tile }
```

A class then names one: `button.frame bevel`, `desktop.background grid`. Adding
a frame style is a theme edit, not a code change, which is the whole point.

### 2.5 The structures

```c
/*
 * A rule is one line of a theme file, already parsed. Rules are sorted by
 * specificity once at load; a lookup is a binary search on (class, key) and
 * then a walk of the few candidates, so drawing never parses anything.
 */
typedef struct Srule Srule;
struct Srule {
	u16int	app;		/* string index, 0 for any */
	u16int	class;		/* Sclass */
	u16int	part;		/* Spart, 0 for the whole */
	u16int	state;		/* Sstate bits: hover, active, disabled, selected, focused */
	u16int	key;		/* Skey */
	u16int	spec;		/* computed specificity, for the sort */
	u32int	valoff;		/* into the theme's value blob */
};

typedef struct Sclassdef Sclassdef;
struct Sclassdef {
	char	*name;
	u16int	parent;		/* classes inherit: item from menu, thumb from scroll */
	u32int	keys;		/* which Skeys are meaningful, for style_explain and prefs */
};

/* what a client asks, and all it needs to ask */
u32int	style_colour(Theme*, int class, int part, int state, int key);
int	style_metric(Theme*, int class, int part, int key);
Sframe *style_frame (Theme*, int class, int part, int state);
Font   *style_font  (Theme*, int class);
```

### 2.6 A preferences window that is generated, not written

Because every class declares which keys apply to it (`Sclassdef.keys`), the
preferences interface is a walk of that table: classes down the side, their
keys in a panel, the current value and where it came from. MUI generated its
prefs this way and it is why MUI applications all had the same settings window
without anyone writing it twice. Here it is one program reading `/n/desktop/theme`
and the class table, and it stays correct when a class gains a key.

## 3. Keys

### 3.1 The dispatcher table already exists

This is the finding that makes the whole feature small. A window manager's
binding system usually needs a table of actions written for the purpose;
`wsys`'s control vocabulary already is one (docs/desktop-protocol.md, section
1): `close`, `delete`, `hide`, `show`, `raise`, `lower`, `focus`, `zoom`,
`move`, `resize`, `workspace n`, `flags`, `snap left|right|top|bottom|full|center`,
`snap grid cols rows i`, and on the desktop `new`, `wsswitch`, `wsnew`,
`lowerall`, `cleanup`, `quit`.

So a bind does not invoke a dispatcher. It **writes a ctl message**, and the
set of things you can bind is the set of things the protocol can already do —
which means every future ctl message is bindable the day it exists, and an
agent driving the desktop and a person pressing a key go down the same path.

### 3.2 The file

`/n/desktop/binds`, read and written like the theme, reloaded live, and read
back formatted so that a program can edit it:

```
# variables, so a person can move the modifier in one place
$mod  = Meta
$alt  = Meta Shift

# bind MODS, KEY, TARGET, MESSAGE...
bind  $mod, Return,     exec,     term
bind  $mod, e,          exec,     edit
bind  $mod, q,          window,   close
bind  $alt, q,          window,   delete
bind  $mod, f,          window,   zoom
bind  $mod, h,          window,   hide
bind  $mod, Left,       window,   snap left
bind  $mod, Right,      window,   snap right
bind  $mod, g,          window,   snap grid 3 2 0
bind  $mod, 1,          desktop,  wsswitch 1
bind  $mod, d,          desktop,  lowerall
bind  $mod, Escape,     desktop,  cleanup

# e: repeats while the key is held
binde $mod, equal,      window,   resize +32 +0
binde $mod, minus,      window,   resize -32 -0

# l: works while the screen is locked
bindl ,    Mute,        exec,     audio mute

# r: fires on release, so a tap does one thing and a hold does another
bindr $mod, Meta_L,     exec,     launcher

# m: a mouse binding; the drag is tracked by the server
bindm $mod, button1,    window,   move
bindm $mod, button3,    window,   resize

# p: pass the key on to the window as well as acting
bindp ,    Print,       exec,     snap -screen
```

### 3.3 Modes, which are Hyprland's submaps and also a leader key

```
mode resize {
	bind , Left,   window, resize -32 +0
	bind , Right,  window, resize +32 +0
	bind , Up,     window, resize +0 -32
	bind , Down,   window, resize +0 +32
	bind , Escape, desktop, mode normal
	bind , Return, desktop, mode normal
}
bind $mod, r, desktop, mode resize

# the same mechanism is a leader: a mode entered by one key, left by any bind
mode leader {
	bind , w, desktop, mode window
	bind , t, exec, term
	timeout 1500                       # and back to normal if nothing follows
}
bind , Space, desktop, mode leader
```

A mode is a named set of binds and a fallback, and it gives sequences without a
second mechanism. The current mode is in `/n/desktop/status` and on the menu
bar, because a modal interface that does not say which mode it is in is a trap.

### 3.4 Window rules, which are where "not a tiling manager" is decided

```
rule app=term            { flags +size; minsize 320 200 }
rule title~"^Debugger"   { workspace 3; snap right }
rule app=edit  first     { snap grid 2 1 0 }        # only the first such window
rule app=wb              { frame thin }             # a theme class, from section 2
rule transient           { centre; flags -size }
```

Rules run when a window is mapped and are a list of ctl messages, so the
vocabulary is again the protocol's. What they deliberately cannot express is
*automatic* layout: nothing here reflows the other windows when one appears,
and there is no master area, no stack, no gap engine and no layout algorithm.

That is the line this design draws. **Hyprland's reach without Hyprland's
model**: you can bind `snap grid 3 2 0` and get a tiled screen in three
keystrokes, and you can bind a mode where the arrow keys place windows into a
grid, and nothing will ever move a window you did not ask it to move. Windows
float; tiling is something the user does, not something the manager imposes.

### 3.5 The structures

```c
typedef enum Btarget : u8int {
	Bwindow = 0,		/* the ctl of the focused window */
	Bdesktop,		/* the desktop ctl */
	Bexec,			/* run a program */
	Bmode,			/* switch modes */
} Btarget;

typedef enum Bflag : u8int {
	Brepeat	= 1 << 0,	/* binde: fires on autorepeat too */
	Blocked	= 1 << 1,	/* bindl: fires while the screen is locked */
	Brelease = 1 << 2,	/* bindr: on key up */
	Bmouse	= 1 << 3,	/* bindm: a button, and the drag is tracked */
	Bpass	= 1 << 4,	/* bindp: the window sees the key as well */
} Bflag;

typedef struct Bind Bind;
struct Bind {
	u16int	mode;		/* which mode it belongs to; 0 is normal */
	u32int	mods;		/* Deskmods */
	u32int	key;		/* a rune, or a Deskkey code for the named keys */
	Btarget	target;
	u8int	flags;
	u16int	pad;
	u32int	msgoff;		/* the ctl message, or the command line */
};
```

Lookup is a hash of (mode, mods, key) built at load. Two binds on one
combination in one mode is an error at load time, reported with both line
numbers, and the previous binding set stays in force — the same rule the theme
parser already follows.

### 3.6 What has to change underneath

| | Change | Size |
|---|---|---|
| `input.c` produces `DK_META` from the scancodes it already decodes | the modifier exists in the enum and nothing sets it | 20 lines |
| Named keys: `Return`, `Escape`, `F1`..`F12`, arrows, `Home`, media keys | a table, matching `keyboard.h`'s runes for the non-printing keys | 100 lines |
| `inputkey` consults the bind table before routing to the focused window | the interception point, which does not exist today | 60 lines |
| Mouse binds need the server to track a drag it started | `grab` already exists in the protocol for this | 120 lines |

## 4. What this buys, and what it costs

| | Work | Exit criterion | Days |
|---|---|---|---|
| D12 | Theme v2: selectors, specificity, the class table, inheritance, the three-file cascade, `style_explain` | a version 1 theme file still loads unchanged, and `term/button.pad 4 2` applies only there | 6 |
| D13 | Named resources: `frame`, `background`, metrics and fonts per class; the client API of 2.5 | `wb` and `term` draw buttons and lists through the theme, and a frame style added in a file changes both | 5 |
| D14 | Binds: the file, variables, `bind` and its five variants, the hash, live reload, conflict detection | every ctl message in the protocol is reachable from a key, and a duplicate bind is refused with both line numbers | 5 |
| D15 | Modes and window rules, with the mode shown in `status` and on the bar | a resize mode, a leader key with a timeout, and a rule placing a window by title | 4 |
| D16 | `DK_META`, the named-key table, the interception point, mouse binds over `grab` | `Meta+drag` moves a window; `Meta+Return` starts a terminal; a `bindp` key reaches the window too | 4 |
| D17 | The generated preferences window, from the class table | every class and key is editable, and the panel says where each value came from | 5 |

Twenty-nine days. D14 and D16 — nine of them — are what turn a desktop you
drive with the mouse into one you drive with both hands, and they are small
precisely because section 3.1 found the dispatcher table already written.

## 5. What is deliberately not here

No automatic tiling, for the reason in 3.4. No scripting language in the
configuration: variables and modes are as far as it goes, because a theme that
can compute is a theme that can fail at draw time, and P7's runtimes are where
programmable behaviour belongs — a script can write these files like anything
else. No per-monitor configuration until there is more than one monitor. And no
animation curves, which is the next thing MUI-flavoured systems reach for and
the first thing to argue about after a frame budget exists (D9) to argue
against.
