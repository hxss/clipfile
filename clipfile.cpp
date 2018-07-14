#include <gdk/gdk.h>
#include <gtk/gtk.h>

enum
{
	TARGET_TEXT_URI_LIST,
	TARGET_GNOME_COPIED_FILES,
	TARGET_UTF8_STRING,
};

static const GtkTargetEntry clipboard_targets[] =
{
	{ (char*)"text/uri-list", 0, TARGET_TEXT_URI_LIST },
	{ (char*)"x-special/gnome-copied-files", 0, TARGET_GNOME_COPIED_FILES },
	{ (char*)"UTF8_STRING", 0, TARGET_UTF8_STRING }
};

enum
{
	ACTION_COPY,
	ACTION_CUT,
	ACTION_CHECK,
	ACTION_PASTE
};

const GString* actions[] = {
	g_string_new("--copy"),
	g_string_new("--cut"),
	g_string_new("--check"),
	g_string_new("--paste")
};

const gchar* prefixs[] = {
	(gchar*)"copy\n",
	(gchar*)"cut\n"
};

static bool
isFilesCopied(GdkAtom* atoms,
	gint n_atoms)
{

	for (int i = 0; i < n_atoms; ++i)
	{
		if (g_strcmp0(gdk_atom_name(atoms[i]), (gchar*)"x-special/gnome-copied-files") == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

char* getAbsolutePath(char* file) {
	if (file && g_str_has_prefix(file, "file://")) {
		file = &file[7];
	}

	return realpath(file, NULL);
}

class FileList {
	public:
		GList* list = NULL;

		FileList(char** files) {
			for (int i = 0; files[i]; ++i) {
				addFile(files[i]);
			}
		}

		void addFile(char* file) {
			char* abs = getAbsolutePath(file);

			if (abs) {
				list = g_list_append(list, g_string_new(abs));
			}

			free(abs);
		}

		GString* toUri() {
			GString* prefix = g_string_new("file://");

			GString* str = g_string_new("");
			GList* l = list;
			while (l != NULL) {
				GList* next = l->next;

				g_string_append_printf(str, "%s%s%c",
					prefix->str,
					((GString*)l->data)->str,
					next != NULL ? '\n' : '\0'
				);

				l = next;
			}

			return str;
		}
};

char* prepareCmd(char* prfx, char* src, char* dst) {
	char* cmd  = (char*)calloc(strlen(prfx) + strlen(src) + strlen(dst) + 2, sizeof(char));
	sprintf(cmd, "%s %s %s", prfx, src, dst);

	return cmd;
}

int copyPath(char* src, char* dst)
{
	// printf("copyPath\n");

	char* prfx = (char*)"cp -r";
	char* cmd  = prepareCmd(prfx, src, dst);

	return system(cmd);
}

int movePath(char* src, char* dst)
{
	// printf("movePath\n");

	char* prfx = (char*)"mv";
	char* cmd  = prepareCmd(prfx, src, dst);

	return system(cmd);
}

struct ClipData {
	bool copy;
	FileList* fl;
};

struct PasteData {
	bool paste;
	GString* path;
};

static void
callbackCopy(GtkClipboard* clipboard,
	GtkSelectionData* selection_data,
	guint             target_info,
	gpointer          user_data)
{
	// printf("callbackCopy\n");

	ClipData* cd = (ClipData*)user_data;
	const gchar* prefix = cd->copy ? prefixs[ACTION_COPY] : prefixs[ACTION_CUT];
	GString *str = cd->fl->toUri();

	switch (target_info)
	{
		case TARGET_GNOME_COPIED_FILES:
			g_string_prepend(str, prefix);

			gtk_selection_data_set(selection_data,
				gtk_selection_data_get_target(selection_data),
				8,
				(guchar*)(str->str),
				str->len
			);
		break;

		case TARGET_TEXT_URI_LIST:
			gtk_selection_data_set_uris(selection_data, &str->str);
		break;

		default:
			gtk_selection_data_set_text(selection_data, str->str, str->len);
	}
}

static void
callbackClear(GtkClipboard* clipboard,
	gpointer user_data)
{
	// printf("callbackClear\n");

	ClipData* cd = (ClipData*)user_data;
	g_free(cd);

	gtk_main_quit();
}

static void
callbackPaste(GtkClipboard* clipboard,
	GtkSelectionData* selection_data,
	gpointer user_data)
{
	// printf("callbackPaste\n");

	PasteData* pd = (PasteData*)user_data;
	const gchar* data = (gchar*)gtk_selection_data_get_data(selection_data);

	gchar** lines = g_strsplit(data, "\n", -1);
	gchar* prefix = g_strconcat(lines[0], "\n", NULL);
	int action = g_strcmp0(prefix, prefixs[ACTION_COPY]) == 0
		? ACTION_COPY
		: ACTION_CUT;

	ClipData* cd = new ClipData();
	cd->fl = new FileList(&lines[1]);

	GList* l = cd->fl->list;
	while (l != NULL) {
		GList* next = l->next;

		switch (action) {
			case ACTION_COPY:
				copyPath(((GString*)l->data)->str, pd->path->str);
				break;
			case ACTION_CUT:
				movePath(((GString*)l->data)->str, pd->path->str);
				gtk_clipboard_set_text(clipboard, "", -1);
				break;
		}

		l = next;
	}

	gtk_main_quit();
}

static void
callbackCheck(GtkClipboard* clipboard,
	GdkAtom* atoms,
	gint n_atoms,
	gpointer user_data)
{
	// printf("callbackCheck\n");

	PasteData* pd = (PasteData*)user_data;
	bool fc = isFilesCopied(atoms, n_atoms);

	if (!pd->paste)
		printf("%d\n", fc);

	if (fc && pd->paste) {
		GdkAtom ga = gdk_atom_intern(clipboard_targets[TARGET_GNOME_COPIED_FILES].target, TRUE);

		gtk_clipboard_request_contents (clipboard,
			ga, callbackPaste,
			pd
		);
	} else {
		gtk_main_quit();
	}
}

int main(int argc, char** argv)
{
	gtk_init(&argc, &argv);

	argc -= 2;
	GString* action = g_string_new(argv[1]);

	GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

	if (g_string_equal(action, actions[ACTION_COPY]) || g_string_equal(action, actions[ACTION_CUT])) {
		// printf("ACTION_COPY\n");

		ClipData* cd = new ClipData();
		cd->fl = new FileList(&argv[2]);
		cd->copy = g_string_equal(action, actions[ACTION_COPY]);

		if (g_list_length(cd->fl->list) > 0) {

			gtk_clipboard_set_with_data(clipboard, clipboard_targets,
				G_N_ELEMENTS (clipboard_targets),
				callbackCopy,
				callbackClear,
				cd);
		} else {
			puts("Incorrect paths");
			exit(1);
		}
	} else if (g_string_equal(action, actions[ACTION_CHECK])) {
		// printf("ACTION_CHECK\n");

		PasteData* pd = new PasteData();
		pd->paste = FALSE;
		gtk_clipboard_request_targets(clipboard,
			callbackCheck,
			pd
		);
	} else if (g_string_equal(action, actions[ACTION_PASTE])) {
		// printf("ACTION_PASTE\n");

		PasteData* pd = new PasteData();
		pd->paste = TRUE;
		pd->path = g_string_new(getAbsolutePath(argv[2]));
		if (pd->path->len == 0)
			pd->path = g_string_new(getAbsolutePath((char*)"."));

		gtk_clipboard_request_targets(clipboard,
			callbackCheck,
			pd
		);
	} else {
		puts("Unknown action");
		exit(1);
	}

	gtk_main();

	return 0;
}
