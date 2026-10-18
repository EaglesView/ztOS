// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ztBrowser: the chrome, in the Windows Phone 8.1 Internet Explorer look.
 *
 * The web page itself is a Wayland subsurface (see webview.c); this file draws
 * only the chrome with the C SDK: the address bar along the bottom, the thin
 * progress line, the tab switcher and the "..." menu. The content area is left
 * blank because the subsurface covers it.
 */
#define _GNU_SOURCE
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "zrnmetrocore.h"
#include "webview.h"

enum page { P_WEB, P_TABS };

enum action {
	ACT_ADDRESS = 1,	/* tap the address bar: edit the URL */
	ACT_RELOAD,		/* reload, or stop while loading */
	ACT_MENU,		/* the "..." */
	ACT_BACK,
	ACT_FORWARD,
	ACT_TABS,		/* open the tab switcher */
	ACT_NEWTAB,
	ACT_TAB,		/* arg: which tab */
	ACT_TAB_CLOSE,		/* arg: which tab */
	ACT_GO,			/* commit the typed URL */
	ACT_MENU_PICK,		/* arg: which menu item */
	ACT_MENU_CANCEL,
};

static const char *const MENU_ITEMS[] = { "tabs", "home", "forward" };
#define MENU_N 3

#define MAX_TABS	12
#define BAR_H		56	/* the bottom address bar */
#define PROGRESS_H	3

struct tab {
	struct webview *view;
	char url[1024];
	char title[256];
	double progress;
	bool loading, secure;
};

static struct {
	struct tab tabs[MAX_TABS];
	int n_tabs, cur;
	bool editing;		/* the address bar is being typed into */
	char edit[1024];
	int edit_len;
	bool menu_open;		/* the "..." menu is showing */
	bool started;		/* the first tab has been opened */
	char data_dir[512];
} B;

static struct tab *tab(void) { return &B.tabs[B.cur]; }

/* ---- the engine's word back to us ---------------------------------------- */

static void on_title(void *user, const char *title) {
	struct tab *t = user;
	snprintf(t->title, sizeof(t->title), "%s", title ? title : "");
	zmc_redraw();
}
static void on_uri(void *user, const char *uri) {
	struct tab *t = user;
	snprintf(t->url, sizeof(t->url), "%s", uri ? uri : "");
	zmc_redraw();
}
static void on_progress(void *user, double f) {
	struct tab *t = user;
	t->progress = f;
	zmc_redraw();
}
static void on_load(void *user, enum wv_load state) {
	struct tab *t = user;
	t->loading = (state == WV_STARTED || state == WV_COMMITTED);
	if (state == WV_FINISHED || state == WV_FAILED) {
		t->progress = 0;
	}
	if (t == tab()) {
		zmc_busy(t->loading);	/* the Windows Phone loading dots */
	}
	zmc_redraw();
}
static void on_secure(void *user, bool ok) {
	struct tab *t = user;
	t->secure = ok;
	zmc_redraw();
}

static const struct wv_delegate DELEGATE_PROTO = {
	.title = on_title, .uri = on_uri, .progress = on_progress,
	.load = on_load, .secure = on_secure,
};

/* ---- tabs ---------------------------------------------------------------- */

static void content_box(int *x, int *y, int *w, int *h) {
	*x = 0;
	*y = 0;
	*w = zmc.width;
	*h = zmc.height - BAR_H;	/* the bar sits at the bottom */
}

static bool tab_open(const char *url) {
	if (B.n_tabs >= MAX_TABS) {
		return false;
	}
	struct tab *t = &B.tabs[B.n_tabs];
	memset(t, 0, sizeof(*t));
	struct wv_delegate d = DELEGATE_PROTO;
	d.user = t;
	t->view = webview_create(zmc.display, zmc.compositor, zmc.surface, B.data_dir, &d);
	if (!t->view) {
		return false;
	}
	int x, y, w, h;
	content_box(&x, &y, &w, &h);
	webview_place(t->view, x, y, w, h, zmc.scale);
	B.cur = B.n_tabs++;
	snprintf(t->url, sizeof(t->url), "%s", url ? url : "");
	if (url && *url) {
		webview_load(t->view, url);
	}
	return true;
}

static void tab_select(int i) {
	if (i < 0 || i >= B.n_tabs) {
		return;
	}
	for (int k = 0; k < B.n_tabs; k++) {
		webview_show(B.tabs[k].view, k == i);
	}
	B.cur = i;
	zmc_busy(B.tabs[i].loading);
}

static void tab_close(int i) {
	if (i < 0 || i >= B.n_tabs) {
		return;
	}
	webview_destroy(B.tabs[i].view);
	for (int k = i; k < B.n_tabs - 1; k++) {
		B.tabs[k] = B.tabs[k + 1];
	}
	B.n_tabs--;
	if (B.n_tabs == 0) {
		tab_open("about:blank");
	}
	if (B.cur >= B.n_tabs) {
		B.cur = B.n_tabs - 1;
	}
	tab_select(B.cur);
}

/* turn what was typed into a URL or a search */
static void navigate(const char *text) {
	char url[1100];
	if (strstr(text, "://")) {
		snprintf(url, sizeof(url), "%s", text);
	} else if (strchr(text, '.') && !strchr(text, ' ')) {
		snprintf(url, sizeof(url), "https://%s", text);
	} else {
		snprintf(url, sizeof(url), "https://duckduckgo.com/?q=%s", text);
	}
	webview_load(tab()->view, url);
}

/* ---- drawing the chrome -------------------------------------------------- */

static void accent(cairo_t *cr) {
	cairo_set_source_rgb(cr, ((zmc.accent >> 16) & 0xff) / 255.0,
		((zmc.accent >> 8) & 0xff) / 255.0, (zmc.accent & 0xff) / 255.0);
}

/* the Windows Phone "windows/tabs" mark: two overlapping squares */
static void tabs_glyph(cairo_t *cr, double cx, double cy) {
	cairo_set_line_width(cr, 2);
	cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
	cairo_rectangle(cr, cx - 9, cy - 5, 13, 13);
	cairo_stroke(cr);
	cairo_rectangle(cr, cx - 3, cy - 11, 13, 13);
	cairo_stroke(cr);
}

static void rounded(cairo_t *cr, double x, double y, double w, double h, double r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -1.5708, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, 1.5708);
	cairo_arc(cr, x + r, y + h - r, r, 1.5708, 3.1416);
	cairo_arc(cr, x + r, y + r, r, 3.1416, 4.7124);
	cairo_close_path(cr);
}

static double draw_web(cairo_t *cr, int page) {
	/* the content area is the subsurface; paint it black underneath so nothing
	 * of an old page shows through while a new one is still blank */
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_rectangle(cr, 0, 0, zmc.width, zmc.height);
	cairo_fill(cr);

	struct tab *t = tab();
	double by = zmc.height - BAR_H;
	if (B.editing) {
		/* keep the bar above the keyboard: its bottom is the window's bottom */
		zmc_osk_box((int)zmc.height);
	}

	/* the bottom bar as Windows Phone 8.1's Internet Explorer draws it: a
	 * near-black strip; on the left a round button with the tabs mark; a
	 * light, nearly square address box with refresh/stop inside its right
	 * end; and the app bar's three dots tucked in the top-right corner */
	cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
	cairo_rectangle(cr, 0, by, zmc.width, BAR_H);
	cairo_fill(cr);

	/* the round tabs button */
	double cx = 26, cy = by + BAR_H / 2.0 + 2;
	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_set_line_width(cr, 2);
	cairo_arc(cr, cx, cy, 15, 0, 2 * 3.14159265);
	cairo_stroke(cr);
	cairo_set_line_width(cr, 1.5);
	cairo_rectangle(cr, cx - 7, cy - 3, 9, 9);
	cairo_stroke(cr);
	cairo_rectangle(cr, cx - 3, cy - 7, 9, 9);
	cairo_stroke(cr);
	zmc_add_hit(0, by, 50, BAR_H, ACT_TABS, 0);

	/* the address box: light grey, 2-point corners, text in near-black */
	double bx = 50, bw = zmc.width - bx - 34, bh = 32, byy = by + (BAR_H - bh) / 2.0 + 2;
	if (B.editing) {
		cairo_set_source_rgb(cr, 1, 1, 1);
	} else {
		cairo_set_source_rgb(cr, 0.86, 0.86, 0.86);
	}
	rounded(cr, bx, byy, bw, bh, 2);
	cairo_fill(cr);
	const char *shown = B.editing ? B.edit : (t->url[0] ? t->url : "");
	cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
	char line[1100];
	snprintf(line, sizeof(line), "%s%s", shown, B.editing ? "|" : "");
	zmc_text_line(cr, line, FONT_REGULAR, 15, bx + 8, byy + 6, bw - 44);
	zmc_add_hit(bx, by, bw - 36, BAR_H, ACT_ADDRESS, 0);

	/* refresh or stop, inside the box at its right end */
	if (!B.editing) {
		cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
		zmc_text(cr, t->loading ? u8"" : u8"", "Segoe MDL2 Assets", 16,
			bx + bw - 28, byy + 8, 0);	/* E711 cancel, E72C refresh */
		zmc_add_hit(bx + bw - 40, by, 40, BAR_H, ACT_RELOAD, 0);
	}

	/* the app bar's dots, small, in the top-right corner of the bar */
	cairo_set_source_rgb(cr, 1, 1, 1);
	for (int i = 0; i < 3; i++) {
		cairo_arc(cr, zmc.width - 22 + i * 6, by + 8, 1.5, 0, 2 * 3.14159265);
		cairo_fill(cr);
	}
	zmc_add_hit(zmc.width - 34, by, 34, BAR_H, ACT_MENU, 0);
	/* (the progress line and the loading dots are drawn on the GPU, in
	 * overlay(), so they move without redrawing this page) */
	/* the "..." menu, from the bottom up */
	if (B.menu_open) {
		zmc_menu(cr, by - MENU_N * 52 - 8, MENU_ITEMS, MENU_N, ACT_MENU_PICK, ACT_MENU_CANCEL);
	}
	return zmc.height;
}

static double draw_tabs(cairo_t *cr, int page) {
	double y = zmc_header(cr, "tabs");
	double cell = (zmc.width - 3 * ZMC_MARGIN) / 2, ch = cell * 0.7;
	for (int i = 0; i < B.n_tabs; i++) {
		double x = ZMC_MARGIN + (i % 2) * (cell + ZMC_MARGIN);
		double ty = y + (i / 2) * (ch + ZMC_MARGIN + 24);
		cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
		cairo_rectangle(cr, x, ty, cell, ch);
		cairo_fill(cr);
		cairo_set_source_rgb(cr, 1, 1, 1);
		zmc_text_line(cr, B.tabs[i].title[0] ? B.tabs[i].title : B.tabs[i].url,
			FONT_SEMILIGHT, 14, x + 8, ty + ch + 2, cell - 16);
		zmc_add_hit(x, ty, cell, ch, ACT_TAB, i);
		/* a close cross in the corner */
		cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
		zmc_text(cr, u8"", "Segoe MDL2 Assets", 13, x + cell - 22, ty + 6, 0);
		zmc_add_hit(x + cell - 30, ty, 30, 30, ACT_TAB_CLOSE, i);
	}
	double ny = y + ((B.n_tabs + 1) / 2) * (ch + ZMC_MARGIN + 24);
	accent(cr);
	cairo_rectangle(cr, ZMC_MARGIN, ny, zmc.width - 2 * ZMC_MARGIN, 48);
	cairo_fill(cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	zmc_text_centred(cr, "new tab", FONT_SEMILIGHT, 17, zmc.width / 2, ny + 13);
	zmc_add_hit(ZMC_MARGIN, ny, zmc.width - 2 * ZMC_MARGIN, 48, ACT_NEWTAB, 0);
	return ny + 70;
}

static double render(cairo_t *cr, int page) {
	switch (page) {
	case P_TABS: return draw_tabs(cr, page);
	default: return draw_web(cr, page);
	}
}

/* ---- input --------------------------------------------------------------- */

static void menu_pick(int which) {
	switch (which) {
	case 0: zmc_go(P_TABS, 1); break;			/* tabs */
	case 1: webview_load(tab()->view, "https://duckduckgo.com"); break;
	case 2: if (webview_can_go_forward(tab()->view)) webview_forward(tab()->view); break;
	}
}

static void activate(int action, int arg) {
	switch (action) {
	case ACT_ADDRESS:
		B.editing = true;
		snprintf(B.edit, sizeof(B.edit), "%s", tab()->url);
		B.edit_len = strlen(B.edit);
		zmc_keyboard(true);
		break;
	case ACT_GO:
		B.editing = false;
		zmc_keyboard(false);
		navigate(B.edit);
		break;
	case ACT_RELOAD:
		if (tab()->loading) {
			webview_stop(tab()->view);
		} else {
			webview_reload(tab()->view, false);
		}
		break;
	case ACT_MENU:
		B.menu_open = true;
		zmc_redraw();
		break;
	case ACT_MENU_PICK:
		B.menu_open = false;
		menu_pick(arg);
		break;
	case ACT_MENU_CANCEL:
		B.menu_open = false;
		zmc_redraw();
		break;
	case ACT_TABS:
		zmc_go(P_TABS, 1);
		break;
	case ACT_NEWTAB:
		tab_open("about:blank");
		zmc_go(P_WEB, -1);
		break;
	case ACT_TAB:
		tab_select(arg);
		zmc_go(P_WEB, -1);
		break;
	case ACT_TAB_CLOSE:
		tab_close(arg);
		break;
	}
}

static bool back(void) {
	if (B.editing) {
		B.editing = false;
		zmc_keyboard(false);
		return true;
	}
	if (zmc.page == P_TABS) {
		zmc_go(P_WEB, -1);
		return true;
	}
	if (webview_can_go_back(tab()->view)) {
		webview_back(tab()->view);
		return true;
	}
	return false;	/* nothing to go back to: close the app */
}

static void text(const char *typed) {
	if (!B.editing) {
		return;
	}
	if (strcmp(typed, "\n") == 0) {
		activate(ACT_GO, 0);
	} else if (strcmp(typed, "\b") == 0) {
		if (B.edit_len > 0) {
			/* step back one UTF-8 character */
			do { B.edit_len--; } while (B.edit_len > 0 && (B.edit[B.edit_len] & 0xc0) == 0x80);
			B.edit[B.edit_len] = 0;
		}
		zmc_redraw();
	} else {
		size_t n = strlen(typed);
		if (B.edit_len + n < sizeof(B.edit) - 1) {
			memcpy(B.edit + B.edit_len, typed, n);
			B.edit_len += n;
			B.edit[B.edit_len] = 0;
		}
		zmc_redraw();
	}
}

/* draw the current page's pixels over the content area, every frame at rest */
static void overlay(int page) {
	if (page != P_WEB || B.n_tabs == 0 || !tab()->view) {
		return;
	}
	int x, y, w, h;
	content_box(&x, &y, &w, &h);
	webview_composite(tab()->view, x, y, w, h);

	struct tab *t = tab();
	if (!t->loading) {
		return;
	}
	double line_y = h - PROGRESS_H;	/* just above the bar */
	if (t->progress > 0.08) {
		zmc_gpu_rect(0, line_y, w * t->progress, PROGRESS_H,
			((zmc.accent >> 16) & 0xff) / 255.0, ((zmc.accent >> 8) & 0xff) / 255.0,
			(zmc.accent & 0xff) / 255.0);
	}
	/* the moving Windows Phone dots come from zmc_busy() (see on_load); its
	 * frames keep this overlay redrawing while the page loads. zmc_present()
	 * must not be called here: it draws at once and would call us again. */
}

/* zmc's loop is Wayland-only; run WebKit's GLib work on a fast tick */
static void pump(void) {
	webview_pump();
}

static char home_url[1024] = "about:blank";

/* the first page has arrived, so the surface exists: open the first tab now */
static void arrived(int page) {
	if (!B.started && page == P_WEB) {
		B.started = true;
		tab_open(home_url);
		zmc_every(16, pump);
	}
}

static const struct zmc_app app = {
	.app_id = "ztbrowser",
	.title = "Internet Explorer",
	.caps = "INTERNET EXPLORER",
	.render = render,
	.activate = activate,
	.back = back,
	.text = text,
	.arrived = arrived,
	.overlay = overlay,
};

int main(int argc, char **argv) {
	const char *home = getenv("HOME");
	snprintf(B.data_dir, sizeof(B.data_dir), "%s/.local/share/ztbrowser", home ? home : "/tmp");
	if (argc > 1 && argv[1][0]) {
		snprintf(home_url, sizeof(home_url), "%s", argv[1]);
	}
	return zmc_run(&app, P_WEB);
}
