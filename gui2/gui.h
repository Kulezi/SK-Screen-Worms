#include <stdlib.h>
#include <math.h>
#include <cairo.h>
#include <glib.h>
#include <gdk/gdkkeysyms-compat.h>
#include <gtk/gtk.h>

extern int area_width, area_height;
extern GtkWidget *drawing_area;

// Gniazdko do poleceń

extern int gsock;

// Opis gracza

typedef struct {
  char player[65];
  GdkColor color;
  GtkWidget *label;
} KolGracz;

// Maks. liczba graczy

#define MAX_PLAYER 20

// Tablica opisów graczy

extern KolGracz kolgracz[];
extern int ilgracz;

// Widget z listą graczy

extern GtkWidget *player_box;

extern int find_player_index (char *player);
extern void draw_brush (GtkWidget *widget, gdouble x, gdouble y, char *player);

extern int process_command (int numtok, char *tokens[]);

extern int init_net (unsigned short port);
