#define AP_IMPLEMENTATION
#include "apostrophe.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

#define MAX_FORMS 128
#define SLOT_COUNT 4

#if !AP_PLATFORM_IS_DEVICE
#ifndef PILL_SHAPES_FONT_PATH
#define PILL_SHAPES_FONT_PATH ""
#endif
#endif

typedef enum {
	SLOT_WHITE_LEFT = 0,
	SLOT_WHITE_RIGHT,
	SLOT_BUTTON_LEFT,
	SLOT_BUTTON_RIGHT
} Slot;

typedef struct {
	const char *key;
	const char *label;
	int base_x;
	int base_y;
	int base_w;
	int base_h;
	int flip;
} SlotInfo;

typedef struct {
	char id[96];
	char label[128];
	char path[PATH_MAX];
	SDL_Texture *texture;
} Form;

typedef struct {
	Form forms[MAX_FORMS];
	int form_count;
	int selected_row;
	int selected[SLOT_COUNT];
	char status[160];
	Uint32 status_until;
	char pak_dir[PATH_MAX];
	char caps_dir[PATH_MAX];
	char res_dir[PATH_MAX];
	char data_dir[PATH_MAX];
	char backup_dir[PATH_MAX];
	char legacy_backup_dir[PATH_MAX];
	char config_path[PATH_MAX];
} App;

static const SlotInfo SLOT_INFO[SLOT_COUNT] = {
	{ "white_left", "Banner Left", 1, 1, 15, 30, 0 },
	{ "white_right", "Banner Right", 16, 1, 15, 30, 1 },
	{ "button_left", "Button Left", 1, 33, 10, 20, 0 },
	{ "button_right", "Button Right", 11, 33, 10, 20, 1 },
};

static void set_status(App *app, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(app->status, sizeof(app->status), fmt, args);
	va_end(args);
	app->status_until = SDL_GetTicks() + 2600;
}

static const char *env_default(const char *name, const char *fallback)
{
	const char *value = getenv(name);
	return value && value[0] ? value : fallback;
}

#if !AP_PLATFORM_IS_DEVICE
static void configure_dev_font(ap_config *cfg, const char *pak_dir)
{
	const char *font_path = getenv("PILL_SHAPES_FONT_PATH");
	if (font_path && font_path[0]) {
		cfg->font_path = font_path;
		return;
	}
	if (PILL_SHAPES_FONT_PATH[0]) {
		cfg->font_path = PILL_SHAPES_FONT_PATH;
		return;
	}

	static char fallback[PATH_MAX];
	snprintf(fallback, sizeof(fallback), "%s/../third_party/apostrophe/res/font.ttf", pak_dir);
	cfg->font_path = fallback;
}
#endif

static int ends_with_png(const char *name)
{
	size_t len = strlen(name);
	return len > 4 && strcasecmp(name + len - 4, ".png") == 0;
}

static void id_to_label(const char *id, char *out, size_t out_size)
{
	size_t oi = 0;
	int word_start = 1;
	for (size_t i = 0; id[i] && oi + 1 < out_size; i++) {
		char c = id[i] == '_' || id[i] == '-' ? ' ' : id[i];
		if (word_start && c >= 'a' && c <= 'z') c = (char)(c - 32);
		out[oi++] = c;
		word_start = c == ' ';
	}
	out[oi] = '\0';
}

static int compare_forms(const void *a, const void *b)
{
	const Form *fa = (const Form *)a;
	const Form *fb = (const Form *)b;
	return strcmp(fa->id, fb->id);
}

static int add_form(App *app, const char *dir_path, const char *file_name)
{
	if (app->form_count >= MAX_FORMS) return 0;

	char id[96];
	snprintf(id, sizeof(id), "%s", file_name);
	char *dot = strrchr(id, '.');
	if (dot) *dot = '\0';

	Form *form = &app->forms[app->form_count++];
	snprintf(form->path, sizeof(form->path), "%s/%s", dir_path, file_name);
	snprintf(form->id, sizeof(form->id), "%s", id);
	id_to_label(form->id, form->label, sizeof(form->label));
	form->texture = NULL;
	return 1;
}

static int load_forms_from_dir(App *app, const char *dir_path)
{
	DIR *dir = opendir(dir_path);
	if (!dir) return 0;

	int loaded = 0;
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (!ends_with_png(ent->d_name)) continue;
		if (add_form(app, dir_path, ent->d_name)) loaded++;
	}
	closedir(dir);
	return loaded;
}

static int load_forms(App *app)
{
	app->form_count = 0;
	load_forms_from_dir(app, app->caps_dir);
	qsort(app->forms, (size_t)app->form_count, sizeof(Form), compare_forms);
	return app->form_count > 0;
}

static int find_form(App *app, const char *id)
{
	for (int i = 0; i < app->form_count; i++) {
		if (strcmp(app->forms[i].id, id) == 0) return i;
	}
	return -1;
}

static int default_form(App *app)
{
	int idx = find_form(app, "circle");
	if (idx >= 0) return idx;
	idx = find_form(app, "rounded");
	if (idx >= 0) return idx;
	idx = find_form(app, "squircle");
	if (idx >= 0) return idx;
	return 0;
}

static void load_config(App *app)
{
	int fallback = default_form(app);
	for (int i = 0; i < SLOT_COUNT; i++) app->selected[i] = fallback;

	FILE *fp = fopen(app->config_path, "r");
	if (!fp) return;

	char line[256];
	while (fgets(line, sizeof(line), fp)) {
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		char *key = line;
		char *value = eq + 1;
		value[strcspn(value, "\r\n")] = '\0';

		for (int i = 0; i < SLOT_COUNT; i++) {
			if (strcmp(key, SLOT_INFO[i].key) != 0) continue;
			int idx = find_form(app, value);
			if (idx >= 0) app->selected[i] = idx;
		}
	}
	fclose(fp);
}

static void save_config(App *app)
{
	FILE *fp = fopen(app->config_path, "w");
	if (!fp) return;
	for (int i = 0; i < SLOT_COUNT; i++) {
		fprintf(fp, "%s=%s\n", SLOT_INFO[i].key, app->forms[app->selected[i]].id);
	}
	fclose(fp);
}

static int ensure_dir(const char *path)
{
	if (mkdir(path, 0755) == 0) return 1;
	return errno == EEXIST;
}

static int ensure_dir_tree(const char *path)
{
	char tmp[PATH_MAX];
	int written = snprintf(tmp, sizeof(tmp), "%s", path);
	if (written <= 0 || written >= (int)sizeof(tmp)) return 0;

	size_t len = strlen(tmp);
	while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';

	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		if (!ensure_dir(tmp)) return 0;
		*p = '/';
	}
	return ensure_dir(tmp);
}

static int copy_file(const char *src, const char *dst)
{
	FILE *in = fopen(src, "rb");
	if (!in) return 0;
	FILE *out = fopen(dst, "wb");
	if (!out) {
		fclose(in);
		return 0;
	}

	char buf[32768];
	size_t n;
	int ok = 1;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) {
			ok = 0;
			break;
		}
	}
	if (ferror(in)) ok = 0;
	fclose(in);
	if (fclose(out) != 0) ok = 0;
	return ok;
}

static SDL_Surface *load_rgba_surface(const char *path)
{
	SDL_Surface *loaded = IMG_Load(path);
	if (!loaded) return NULL;
	SDL_Surface *rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(loaded);
	return rgba;
}

static SDL_Surface *resize_surface(SDL_Surface *src, int w, int h)
{
	SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
	if (!dst) return NULL;
	SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
	SDL_Rect dst_rect = { 0, 0, w, h };
	if (SDL_BlitScaled(src, NULL, dst, &dst_rect) != 0) {
		SDL_FreeSurface(dst);
		return NULL;
	}
	return dst;
}

static SDL_Surface *flip_surface_horizontal(SDL_Surface *src)
{
	SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA32);
	if (!dst) return NULL;

	if (SDL_LockSurface(src) != 0) {
		SDL_FreeSurface(dst);
		return NULL;
	}
	if (SDL_LockSurface(dst) != 0) {
		SDL_UnlockSurface(src);
		SDL_FreeSurface(dst);
		return NULL;
	}

	for (int y = 0; y < src->h; y++) {
		Uint8 *srow = (Uint8 *)src->pixels + y * src->pitch;
		Uint8 *drow = (Uint8 *)dst->pixels + y * dst->pitch;
		for (int x = 0; x < src->w; x++) {
			memcpy(drow + (src->w - 1 - x) * 4, srow + x * 4, 4);
		}
	}

	SDL_UnlockSurface(dst);
	SDL_UnlockSurface(src);
	return dst;
}

static int patch_slot(SDL_Surface *assets, App *app, Slot slot, int scale)
{
	SlotInfo info = SLOT_INFO[slot];
	Form *form = &app->forms[app->selected[slot]];
	SDL_Surface *cap_src = load_rgba_surface(form->path);
	if (!cap_src) return 0;

	SDL_Surface *cap = resize_surface(cap_src, info.base_w * scale, info.base_h * scale);
	SDL_FreeSurface(cap_src);
	if (!cap) return 0;

	if (info.flip) {
		SDL_Surface *flipped = flip_surface_horizontal(cap);
		SDL_FreeSurface(cap);
		cap = flipped;
		if (!cap) return 0;
	}

	SDL_SetSurfaceBlendMode(cap, SDL_BLENDMODE_NONE);
	SDL_Rect dst = {
		info.base_x * scale,
		info.base_y * scale,
		info.base_w * scale,
		info.base_h * scale
	};
	int ok = SDL_BlitSurface(cap, NULL, assets, &dst) == 0;
	SDL_FreeSurface(cap);
	return ok;
}

static int apply_scale(App *app, int scale)
{
	char src_path[PATH_MAX];
	char backup_path[PATH_MAX];
	char legacy_userdata_backup_path[PATH_MAX];
	char legacy_backup_path[PATH_MAX];
	snprintf(src_path, sizeof(src_path), "%s/assets@%dx.png", app->res_dir, scale);
	snprintf(backup_path, sizeof(backup_path), "%s/assets@%dx.png", app->backup_dir, scale);
	snprintf(legacy_userdata_backup_path,
		sizeof(legacy_userdata_backup_path),
		"%s/assets@%dx.png",
		app->legacy_backup_dir,
		scale);
	snprintf(legacy_backup_path, sizeof(legacy_backup_path), "%s/backup/assets@%dx.png", app->pak_dir, scale);

	if (access(backup_path, F_OK) != 0) {
		if (access(legacy_userdata_backup_path, F_OK) == 0) {
			if (!copy_file(legacy_userdata_backup_path, backup_path)) return 0;
		} else if (access(legacy_backup_path, F_OK) == 0) {
			if (!copy_file(legacy_backup_path, backup_path)) return 0;
		} else if (!copy_file(src_path, backup_path)) {
			return 0;
		}
	}

	SDL_Surface *loaded = IMG_Load(src_path);
	if (!loaded) return 0;
	SDL_Surface *assets = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(loaded);
	if (!assets) return 0;

	int ok = 1;
	for (int slot = 0; slot < SLOT_COUNT; slot++) {
		if (!patch_slot(assets, app, (Slot)slot, scale)) {
			ok = 0;
			break;
		}
	}

	if (ok && IMG_SavePNG(assets, src_path) != 0) ok = 0;
	SDL_FreeSurface(assets);
	return ok;
}

static int apply_assets(App *app)
{
	if (!ensure_dir_tree(app->backup_dir)) {
		set_status(app, "Backup directory failed");
		return 0;
	}

	ap_log("Apply requested");
	for (int scale = 1; scale <= 4; scale++) {
		if (!apply_scale(app, scale)) {
			set_status(app, "Apply failed at @%dx", scale);
			return 0;
		}
	}

	char marker[PATH_MAX];
	snprintf(marker, sizeof(marker), "%s/.pill_shapes_preset", app->res_dir);
	FILE *fp = fopen(marker, "w");
	if (fp) {
		fprintf(fp, "ui\n");
		fclose(fp);
	}
	save_config(app);
	ap_reload_status_assets();
	set_status(app, "Applied");
	return 1;
}

static int restore_assets(App *app)
{
	for (int scale = 1; scale <= 4; scale++) {
		char src[PATH_MAX];
		char legacy_userdata_src[PATH_MAX];
		char legacy_src[PATH_MAX];
		char dst[PATH_MAX];
		snprintf(src, sizeof(src), "%s/assets@%dx.png", app->backup_dir, scale);
		snprintf(legacy_userdata_src,
			sizeof(legacy_userdata_src),
			"%s/assets@%dx.png",
			app->legacy_backup_dir,
			scale);
		snprintf(legacy_src, sizeof(legacy_src), "%s/backup/assets@%dx.png", app->pak_dir, scale);
		snprintf(dst, sizeof(dst), "%s/assets@%dx.png", app->res_dir, scale);
		if (access(src, F_OK) != 0) {
			if (access(legacy_userdata_src, F_OK) == 0) {
				snprintf(src, sizeof(src), "%s", legacy_userdata_src);
			} else if (access(legacy_src, F_OK) == 0) {
				snprintf(src, sizeof(src), "%s", legacy_src);
			} else {
				set_status(app, "No backup found");
				return 0;
			}
		}
		if (!copy_file(src, dst)) {
			set_status(app, "Restore failed at @%dx", scale);
			return 0;
		}
	}

	char marker[PATH_MAX];
	snprintf(marker, sizeof(marker), "%s/.pill_shapes_preset", app->res_dir);
	unlink(marker);
	ap_reload_status_assets();
	set_status(app, "Restored");
	return 1;
}

static SDL_Texture *form_texture(App *app, int idx)
{
	if (idx < 0 || idx >= app->form_count) return NULL;
	Form *form = &app->forms[idx];
	if (!form->texture) form->texture = ap_load_image(form->path);
	return form->texture;
}

static void draw_cap(App *app, int form_idx, int x, int y, int w, int h, int flip, ap_color color)
{
	SDL_Texture *tex = form_texture(app, form_idx);
	if (!tex) return;
	SDL_SetTextureColorMod(tex, color.r, color.g, color.b);
	SDL_SetTextureAlphaMod(tex, color.a);
	SDL_Rect dst = { x, y, w, h };
	SDL_RenderCopyEx(ap_get_renderer(), tex, NULL, &dst, 0.0, NULL, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
	SDL_SetTextureColorMod(tex, 255, 255, 255);
	SDL_SetTextureAlphaMod(tex, 255);
}

static void draw_custom_pill(App *app, int left_idx, int right_idx, int x, int y, int w, int h, ap_color color)
{
	int half = h / 2;
	if (w < h) w = h;
	ap_draw_rect(x + half, y, w - h, h, color);
	draw_cap(app, left_idx, x, y, half, h, 0, color);
	draw_cap(app, right_idx, x + w - half, y, half, h, 1, color);
}

static void draw_centered_text(TTF_Font *font, const char *text, SDL_Rect rect, ap_color color)
{
	int text_w = ap_measure_text(font, text);
	int text_h = TTF_FontHeight(font);
	ap_draw_text(font,
		text,
		rect.x + (rect.w - text_w) / 2,
		rect.y + (rect.h - text_h) / 2,
		color);
}

static int preview_button_width(TTF_Font *font, const char *text, int button_h)
{
	int text_w = ap_measure_text(font, text);
	return strlen(text) == 1 ? button_h : button_h / 2 + text_w;
}

static int preview_footer_item_width(TTF_Font *button_font,
	TTF_Font *label_font,
	const char *button,
	const char *label,
	int button_h,
	int gap)
{
	return preview_button_width(button_font, button, button_h) +
		gap +
		ap_measure_text(label_font, label) +
		gap;
}

static void draw_preview_footer_item(App *app,
	int *x,
	int y,
	int button_h,
	int gap,
	const char *button,
	const char *label)
{
	ap_theme *theme = ap_get_theme();
	TTF_Font *button_font = strlen(button) == 1
		? ap_get_font(AP_FONT_MEDIUM)
		: ap_get_font(AP_FONT_TINY);
	TTF_Font *label_font = ap_get_font(AP_FONT_SMALL);
	int button_w = preview_button_width(button_font, button, button_h);

	draw_custom_pill(app,
		app->selected[SLOT_BUTTON_LEFT],
		app->selected[SLOT_BUTTON_RIGHT],
		*x,
		y,
		button_w,
		button_h,
		theme->highlight);
	draw_centered_text(button_font,
		button,
		(SDL_Rect){ *x, y, button_w, button_h },
		theme->button_label);

	*x += button_w + gap;
	ap_draw_text(label_font,
		label,
		*x,
		y + (button_h - TTF_FontHeight(label_font)) / 2,
		theme->hint);
	*x += ap_measure_text(label_font, label) + gap;
}

static void render(App *app)
{
	ap_theme *theme = ap_get_theme();
	TTF_Font *item_font = ap_get_font(AP_FONT_MEDIUM);
	TTF_Font *small_font = ap_get_font(AP_FONT_SMALL);
	int screen_w = ap_get_screen_width();
	int screen_h = ap_get_screen_height();
	int margin = AP_DS(ap__g.device_padding);
	int row_h = AP_DS(AP__PILL_SIZE);
	int preview_h = AP_DS(AP__PILL_SIZE);
	int preview_y = AP_DS(ap__g.device_padding);
	int list_y = preview_y + preview_h + AP_DS(AP__BUTTON_MARGIN * 2);

	ap_draw_background();

	if (app->form_count <= 0) {
		const char *message = "No forms found";
		int text_w = ap_measure_text(item_font, message);
		ap_draw_text(item_font,
			message,
			(screen_w - text_w) / 2,
			(screen_h - TTF_FontHeight(item_font)) / 2,
			theme->hint);
		ap_footer_item footer[] = {
			{ .button = AP_BTN_B, .label = "BACK" },
		};
		ap_draw_footer(footer, 1);
		return;
	}

	int pill_h = AP_DS(AP__PILL_SIZE);
	int pill_y = preview_y;
	int button_gap = AP_DS(AP__BUTTON_MARGIN);
	int button_h = AP_DS(AP__BUTTON_SIZE);
	TTF_Font *button_text_font = ap_get_font(AP_FONT_TINY);
	TTF_Font *single_button_font = ap_get_font(AP_FONT_MEDIUM);
	int first_item_w = preview_footer_item_width(button_text_font,
		small_font,
		"Button",
		"Banner",
		button_h,
		button_gap);
	int second_item_w = preview_footer_item_width(single_button_font,
		small_font,
		"X",
		"Banner",
		button_h,
		button_gap);
	int preview_w = button_gap + first_item_w + button_gap + second_item_w + button_gap;
	if (preview_w > screen_w - margin * 2) preview_w = screen_w - margin * 2;

	draw_custom_pill(app,
		app->selected[SLOT_WHITE_LEFT],
		app->selected[SLOT_WHITE_RIGHT],
		margin,
		pill_y,
		preview_w,
		pill_h,
		theme->accent);

	int preview_x = margin + button_gap;
	int button_y = pill_y + button_gap;
	draw_preview_footer_item(app, &preview_x, button_y, button_h, button_gap, "Button", "Banner");
	preview_x += button_gap;
	draw_preview_footer_item(app, &preview_x, button_y, button_h, button_gap, "X", "Banner");

	for (int i = 0; i < SLOT_COUNT; i++) {
		int y = list_y + i * row_h;
		int selected = i == app->selected_row;
		if (selected) {
			ap_draw_pill(margin, y, screen_w - margin * 2, row_h, theme->highlight);
		}

		ap_color label_color = selected ? theme->highlighted_text : theme->text;
		ap_color value_color = selected ? theme->highlighted_text : theme->hint;
		int text_y = y + (row_h - TTF_FontHeight(item_font)) / 2;
		ap_draw_text(item_font, SLOT_INFO[i].label, margin + AP_DS(AP__BUTTON_PADDING), text_y, label_color);

		const char *value = app->forms[app->selected[i]].label;
		int value_w = ap_measure_text(item_font, value);
		ap_draw_text(item_font, value, screen_w - margin - AP_DS(AP__BUTTON_PADDING) - value_w, text_y, value_color);
	}

	if (app->status[0] && SDL_GetTicks() < app->status_until) {
		int y = screen_h - ap_get_footer_height() - AP_DS(AP__PILL_SIZE + AP__BUTTON_MARGIN);
		int w = ap_measure_text(small_font, app->status);
		ap_draw_text(small_font, app->status, (screen_w - w) / 2, y, theme->hint);
	}

	ap_footer_item footer[] = {
		{ .button = AP_BTN_B, .label = "BACK" },
		{ .button = AP_BTN_X, .label = "RESTORE" },
		{ .button = AP_BTN_A, .label = "APPLY", .is_confirm = true },
	};
	ap_draw_footer(footer, 3);
}

static void cycle_form(App *app, int delta)
{
	if (app->form_count <= 0) {
		set_status(app, "No forms found");
		return;
	}
	int *idx = &app->selected[app->selected_row];
	*idx = (*idx + delta + app->form_count) % app->form_count;
}

static int handle_input(App *app, const ap_input_event *ev)
{
	if (!ev->pressed) return 1;

	switch (ev->button) {
	case AP_BTN_UP:
		app->selected_row = (app->selected_row + SLOT_COUNT - 1) % SLOT_COUNT;
		break;
	case AP_BTN_DOWN:
		app->selected_row = (app->selected_row + 1) % SLOT_COUNT;
		break;
	case AP_BTN_LEFT:
		cycle_form(app, -1);
		break;
	case AP_BTN_RIGHT:
		cycle_form(app, 1);
		break;
	case AP_BTN_A:
		if (app->form_count > 0) {
			apply_assets(app);
		} else {
			set_status(app, "No forms found");
		}
		break;
	case AP_BTN_X:
		restore_assets(app);
		break;
	case AP_BTN_B:
	case AP_BTN_MENU:
		return 0;
	default:
		break;
	}
	return 1;
}

int main(int argc, char **argv)
{
	App app;
	memset(&app, 0, sizeof(app));
	app.selected_row = 0;

	const char *pak_dir = env_default("PILL_SHAPES_PAK_DIR", ".");
	const char *sdcard = env_default("SDCARD_PATH", "/mnt/SDCARD");
	const char *userdata = env_default("USERDATA_PATH", "/mnt/SDCARD/.userdata/tg5040");
	snprintf(app.pak_dir, sizeof(app.pak_dir), "%s", pak_dir);
	snprintf(app.caps_dir, sizeof(app.caps_dir), "%s/caps", pak_dir);
	snprintf(app.res_dir, sizeof(app.res_dir), "%s/.system/res", sdcard);
	snprintf(app.data_dir, sizeof(app.data_dir), "%s/shaper", userdata);
	snprintf(app.backup_dir, sizeof(app.backup_dir), "%s/backup", app.data_dir);
	snprintf(app.legacy_backup_dir, sizeof(app.legacy_backup_dir), "%s/pill-shapes/backup", userdata);
	snprintf(app.config_path, sizeof(app.config_path), "%s/shaper.cfg", userdata);

	ap_config cfg = {0};
	cfg.window_title = "Shaper";
	cfg.is_nextui = true;
	cfg.cpu_speed = AP_CPU_SPEED_MENU;
#if !AP_PLATFORM_IS_DEVICE
	configure_dev_font(&cfg, pak_dir);
	cfg.disable_background = true;
#endif

	if (ap_init(&cfg) != AP_OK) return 1;

	if (!load_forms(&app)) {
		set_status(&app, "No forms found");
	} else {
		load_config(&app);
	}

	if (argc > 1 && strcmp(argv[1], "--apply") == 0) {
		int ok = app.form_count > 0 && apply_assets(&app);
		ap_quit();
		return ok ? 0 : 1;
	}
	if (argc > 1 && strcmp(argv[1], "--restore") == 0) {
		int ok = restore_assets(&app);
		ap_quit();
		return ok ? 0 : 1;
	}

	int running = 1;
	while (running) {
		ap_input_event ev;
		while (ap_poll_input(&ev)) {
			running = handle_input(&app, &ev);
			if (!running) break;
		}

		render(&app);
		if (app.status[0] && SDL_GetTicks() < app.status_until) ap_request_frame_in(250);
		ap_present();
	}

	for (int i = 0; i < app.form_count; i++) {
		if (app.forms[i].texture) SDL_DestroyTexture(app.forms[i].texture);
	}
	ap_quit();
	return 0;
}
