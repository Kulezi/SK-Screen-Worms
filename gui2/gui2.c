#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <gdk/gdkkeysyms-compat.h>
#include <gtk/gtk.h>

#include "err.h"
#include "read_line.h"
#include "gui.h"

// Maks. długość komunikatu

#define BUFFER_SIZE 2000

// Maks. liczba niepustych tokenów w komunikacie wejściowym

#define MAX_TOKENS 30

int gsock = -1;  // gniazdko do poleceń

GtkWidget *drawing_area = NULL;  // pole gry
int area_width = 20, area_height = 20;  // początkowe rozmiary

GdkColor color;  // bieżący kolor

KolGracz kolgracz[MAX_PLAYER];  // lista opisów graczy
int ilgracz = 0;  // bieżąca liczba graczy

GtkWidget *player_box;  // widget z listą graczy

// Czy rozgrywka wystartowała?

gboolean started = FALSE;

static void arrow_pressed (GtkButton *widget, gpointer data);
static void arrow_released (GtkButton *widget, gpointer data);
static gboolean configure_event (GtkWidget *widget, GdkEventConfigure *event,
                                 gpointer data);
static GtkWidget *create_arrow_button (GtkArrowType arrow_type, 
                                       GtkShadowType shadow_type);
static gint destroy_window (GtkWidget *widget, GdkEvent *event, 
                            gpointer data);
static gboolean expose_event (GtkWidget *widget, GdkEventExpose *event,
                              gpointer data);
static gboolean idle_callback (gpointer data);
static void init_colors (void);
static gint keyboard_event (GtkWidget *widget, GdkEventKey *event,
                            gpointer data);
static int remove_empty_tokens (char *tokens[], char *result[]);
static void send_message (char* message);

// Proaktywne inicjowanie kolorów

//!!! Przydałoby się więcej kolorów, ale nie mam zdrowia wymyślać.

void init_colors () {
  char *colors[] = {"red", "green", "blue", "yellow", "brown", "cyan",
                    "magenta", "black", "violet", NULL};
  GdkColor color;

  for (int i = 0; i < MAX_PLAYER && colors[i] != NULL; i++) {
    if (!gdk_color_parse(colors[i], &color))
      syserr("Invalid color spec");
    kolgracz[i].color = color;
  }
}

// Szukanie opisu gracza po nazwie, zwraca indeks w tablicy opisów

int find_player_index (char *player) {
  for (int i = 0; i < ilgracz; i++)
    if (strncmp(kolgracz[i].player, player, 64) == 0)  //z protokołu
      return i;
#ifdef DEBUG
  fprintf(stderr, "Brak gracza w tablicy");
#endif
  return -1;
}

// Usuwanie pustych tokenów z tablicy tokens, wynik w result,
// maks. MAX_TOKENS tokenów w wyniku.  Zwraca liczbę (niepustych)
// tokenów.

int remove_empty_tokens (char *tokens[], char *result[]) {
  int i;
  int j = 0;

  for (i = 0; tokens[i] != NULL; i++) {
    if (strlen(tokens[i]) != 0)
      result[j++] = tokens[i];
    if (j > MAX_TOKENS)
      syserr("Too many tokens");
  }
  result[j] = NULL;
  return j;
}

// Okresowy callback do komunikacji z siecią

gboolean idle_callback (gpointer data) {
  if (started) {
    char buffer[BUFFER_SIZE];
    ssize_t len;

    memset(buffer, 0, sizeof(buffer));
    len = readLine(gsock, buffer, sizeof(buffer));

    if (len < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return G_SOURCE_CONTINUE;
      else
        syserr("reading error");
    }
    else if (len == 0) {
#ifdef DEBUG
      fprintf(stderr, "Pusty komunikat - koniec połączenia\n");
#endif
      close(gsock);
      exit(1);
    }
    else {
      char **raw_tokens, *tokens[MAX_TOKENS + 1];
      int numtok;

#ifdef DEBUG
      fprintf(stderr, "Command:%s\n", buffer);
#endif
      raw_tokens = g_strsplit_set(buffer, " \t\n\r", 0);
      numtok = remove_empty_tokens(raw_tokens, tokens);

      process_command(numtok, tokens);
    }
  }
  return G_SOURCE_CONTINUE;
}

// Tworzenie przycisku ze strzałką (alternatywa klawiatury)

GtkWidget *create_arrow_button (GtkArrowType arrow_type, 
                                GtkShadowType shadow_type) {
  GtkWidget *button;
  GtkWidget *arrow;

  button = gtk_button_new();
  arrow = gtk_arrow_new(arrow_type, shadow_type);

  gtk_container_add(GTK_CONTAINER(button), arrow);
  
  gtk_widget_show(button);
  gtk_widget_show(arrow);

  return button;
}

// Wysyłanie komunikatu

void send_message (char* message) {
  ssize_t len = strlen(message);
  ssize_t snd_len = write(gsock, message, len);

#ifdef DEBUG
  fprintf(stderr, "Wysyłam:%s\n", message);
#endif
  if (snd_len != len)
    syserr("writing to client socket");
}

// Callbacki dla przycisków strzałek

void arrow_pressed (GtkButton *widget, gpointer data) {
  if (strcmp(data, "left") == 0)
    send_message("LEFT_KEY_DOWN\n");
  else
    send_message("RIGHT_KEY_DOWN\n");
  started = TRUE;  
}

void arrow_released (GtkButton *widget, gpointer data) {
  if (strcmp(data, "left") == 0)
    send_message("LEFT_KEY_UP\n");
  else
    send_message("RIGHT_KEY_UP\n");
  started = TRUE;  
}

// Callback dla klawiatury

gint keyboard_event (GtkWidget *widget, GdkEventKey *event, gpointer data) {
  if (event->keyval == GDK_Left) {
    if (event->type == GDK_KEY_PRESS)
      send_message("LEFT_KEY_DOWN\n");
    else if (event->type == GDK_KEY_RELEASE)
      send_message("LEFT_KEY_UP\n");
  }
  else if (event->keyval == GDK_Right) {
    if (event->type == GDK_KEY_PRESS)
      send_message("RIGHT_KEY_DOWN\n");
    else if (event->type == GDK_KEY_RELEASE)
      send_message("RIGHT_KEY_UP\n");
  }

  started = TRUE;  
  return TRUE;
}

// "Piksmapa" z kopią pola gry

cairo_surface_t *surface = NULL;

// Inicjowanie kopii pola gry nowym rozmiarem pola.  Uwaga: nie zachowuje 
// dotychczasowej zawartości => pole gry zostanie wyczyszczone.

gboolean configure_event (GtkWidget *widget, GdkEventConfigure *event,
                          gpointer data) {
  GtkAllocation allocation;
  cairo_t *cr;

  if (surface != NULL)
    cairo_surface_destroy(surface);

  gtk_widget_get_allocation(widget, &allocation);
  surface = gdk_window_create_similar_surface(gtk_widget_get_window(widget),
                                              CAIRO_CONTENT_COLOR,
                                              allocation.width,
                                              allocation.height);

  // Czyszczenie tła
  cr = cairo_create(surface);
  cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  cairo_paint(cr);
  cairo_destroy(cr);

  return TRUE;
}

// Odrysowanie pola gry z kopii

gboolean expose_event (GtkWidget *widget, GdkEventExpose *event,
                       gpointer data) {
  cairo_t *cr;

  cr = gdk_cairo_create(gtk_widget_get_window(widget));
  cairo_set_source_surface(cr, surface, 0.0, 0.0);
  cairo_paint(cr);
  cairo_destroy(cr);

  return FALSE;
}

// Rysowanie nowego punktu w polu gry (mały kwadrat wygląda lepiej)

void draw_brush (GtkWidget *widget, gdouble x, gdouble y, char* player) {
  int index = find_player_index(player);

  if (index >= 0) {
    cairo_t *cr = cairo_create(surface);
    GdkColor color = kolgracz[index].color;

    gdk_cairo_set_source_color(cr, &color);
    cairo_rectangle(cr, (x - 1.0), (y - 1.0), 3.0, 3.0);
    cairo_fill(cr);
    cairo_destroy(cr);

    gtk_widget_queue_draw_area(widget,
                               (int)(x - 1.0),
                               (int)(y - 1.0),
                               3,
                               3);
  }
}

// Obecnie nie używana.

int area_clear (GtkWidget *widget, gpointer data) {
  //cairo_t *cr = cairo_create(surface);

  //cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  //cairo_paint(cr);
  //cairo_destroy(cr);
  //gtk_widget_queue_draw(widget);
  return TRUE;
}

// Wyjście z pętli Gtk

gint destroy_window (GtkWidget *widget, GdkEvent *event, gpointer data) {
  close(gsock);
  gtk_main_quit();
  return TRUE;
}

// Zaczynamy

gint main (gint argc, gchar *argv[]) {
  GtkWidget *window, *frame;
  GtkWidget *box1, *box2, *box3;
  GtkWidget *event_box;
  GtkWidget *button;
  int idle_id;

  unsigned short port = 20210;  //default

  // Opcjonalny port
  if (argc > 1)
    port = atoi(argv[1]);

  init_net(port);

  // Inicjowanie Gtk, automatyczne obrobienie gtk-related opcji
  gtk_init(&argc, &argv);

  init_colors();

  // Ustawienie callbacka dla idle
  idle_id = g_idle_add(idle_callback, &started);
  
  // Utworzenie głównego okna aplikacji
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW (window), "Screen Worms GUI");
  gtk_window_set_default_size(GTK_WINDOW (window), 640, 480);
  gtk_window_set_resizable(GTK_WINDOW (window), TRUE);  /***/

  // Callback dla zamknięcia okna przez użytkownika
  g_signal_connect(window, "delete-event",
                   G_CALLBACK(destroy_window), NULL);

  // Maska zdarzeń dla klawiatury
  gtk_widget_set_events(window,
                        GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);

  // Callbacki dla klawiatury
  g_signal_connect(window, "key-press-event",
		   G_CALLBACK(keyboard_event), "press");
  g_signal_connect(window, "key-release-event", 
                   G_CALLBACK(keyboard_event), "release");
    
  // Kontener główny
  box1 = gtk_vbox_new(FALSE, 0);
  
  // Kontener do przycisków sterujących
  box2 = gtk_hbox_new(FALSE, 0);

  // Przycisk strzałki w lewo
  button = create_arrow_button(GTK_ARROW_LEFT, GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start(GTK_BOX(box2), button, FALSE, FALSE, 3);
  g_signal_connect(button, "pressed",
                   G_CALLBACK(arrow_pressed), (gpointer)"left");
  g_signal_connect(button, "released",
                   G_CALLBACK(arrow_released), (gpointer)"left");
  
  // Przycisk strzałki w prawo
  button = create_arrow_button(GTK_ARROW_RIGHT, GTK_SHADOW_ETCHED_OUT);
  gtk_box_pack_start(GTK_BOX(box2), button, FALSE, FALSE, 3);
  g_signal_connect(button, "pressed",
                   G_CALLBACK(arrow_pressed), (gpointer)"right");
  g_signal_connect(button, "released",
                   G_CALLBACK(arrow_released), (gpointer)"right");

  // Przycisk do czyszczenia ekranu (nie używany)
  button = gtk_button_new_with_label("Clear");
  gtk_box_pack_end(GTK_BOX(box2), button, FALSE, FALSE, 3);
  g_signal_connect(button, "clicked",
                   G_CALLBACK(area_clear), (gpointer)drawing_area);

  // Kontener do pola gry i listy graczy.
  box3 = gtk_hbox_new(FALSE, 0);

  // Tworzenie pola gry
  frame = gtk_frame_new(NULL);
  drawing_area = gtk_drawing_area_new();
  gtk_widget_set_size_request(GTK_WIDGET(drawing_area),
                              area_width, area_height);

  // Koloryzacja tła
  color.red = 0;
  color.blue = 65535;
  color.green = 0;
  gtk_widget_modify_bg(drawing_area, GTK_STATE_NORMAL, &color);       

  // Obsługa kopii pola gry
  g_signal_connect(drawing_area, "expose-event",
                   G_CALLBACK(expose_event), NULL);
  g_signal_connect(drawing_area, "configure-event",
                   G_CALLBACK(configure_event), NULL);
    
  // Tworzenie kontenera do dynamicznej listy graczy
  event_box = gtk_event_box_new();  // aby dostawać zdarzenia
  player_box = gtk_vbox_new(FALSE, 0);
  gtk_widget_set_size_request(player_box, 100, -1);
  {
    GdkColor color;

    if (gdk_color_parse("white", &color))
      gtk_widget_modify_bg(event_box, GTK_STATE_NORMAL, &color);
    else
      syserr("player color");
  }
  gtk_container_add(GTK_CONTAINER(event_box), player_box);
  
  // Pakowanie wszystkiego
  gtk_box_pack_start(GTK_BOX(box1), box2, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box1), box3, TRUE, TRUE, 0);
  gtk_container_add(GTK_CONTAINER(frame), drawing_area);
  gtk_box_pack_start(GTK_BOX(box3), frame, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(box3), event_box, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(window), box1);

  // Pokazać to
  gtk_widget_show_all(window);

  // Obecnie zbędne, klawiatura działa wszędzie (tzn. w głównym oknie)
  //gtk_widget_set_can_focus(drawing_area, TRUE);
  //gtk_widget_grab_focus(drawing_area);
  
  // Pętla główna
  gtk_main();

  // Dla marudnych kompilatorów
  return 0;
}

/*EOF*/
