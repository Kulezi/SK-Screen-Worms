#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "gui.h"

static gboolean all_digits (char* string) {
  for (int i = 0; i < strlen(string); i++)
    if (!isdigit(string[i]))
      return FALSE;
  return TRUE;
}

int process_command (int numtok, char *tokens[]) {
  if (strcmp(tokens[0], "NEW_GAME") == 0 && numtok > 4) {
    if (!all_digits(tokens[1]) || !all_digits(tokens[2]))
      return 0;
    // Czyszczenie listy graczy
    if (ilgracz > 0)
      for (int i = 0; i < ilgracz; i++) {
        gtk_widget_destroy(kolgracz[i].label);
        kolgracz[i].label = NULL;
      }

    // Inicjowanie pola gry i listy graczy
    area_width = atoi(tokens[1]);
    area_height = atoi(tokens[2]);
    gtk_widget_set_size_request(drawing_area, area_width - 2, area_height - 2);
    gtk_widget_set_size_request(drawing_area, area_width, area_height);

    ilgracz = numtok - 3;
    if (ilgracz < 1)
      return 0;
    if (ilgracz > MAX_PLAYER) {
      fprintf(stderr, "Warning: too many players\n");
      ilgracz = MAX_PLAYER;
    }

    for (int i = 0; i < ilgracz; i++) {
      GtkWidget *label;

      strcpy(kolgracz[i].player, tokens[i + 3]);
      label = gtk_label_new(tokens[i + 3]);
      kolgracz[i].label = label;
      gtk_widget_modify_fg(label, GTK_STATE_NORMAL, &(kolgracz[i].color));
      gtk_box_pack_start(GTK_BOX(player_box), label, FALSE, FALSE, 3);
      gtk_widget_show(label);
    }
#ifdef DEBUG
    fprintf(stderr, "NEW_GAME command accepted\n");
#endif
    return 1;
  }
  else if (strcmp(tokens[0], "PIXEL") == 0) {
    // Rysowanie kolejnego punktu
    if (numtok == 4 && all_digits(tokens[1]) && all_digits(tokens[2])) {
      draw_brush(drawing_area, atoi(tokens[1]), atoi(tokens[2]), tokens[3]);
#ifdef DEBUG
      fprintf(stderr, "PIXEL command accepted\n");
#endif
      return 1;
    }
    else return 0;
  }
  else if (strcmp(tokens[0], "PLAYER_ELIMINATED") == 0) {
    // Markowanie gracza
    if (numtok == 2) {
      int index = find_player_index(tokens[1]);
      char buf[66];

      memset(buf, 0, sizeof(buf));
      strcpy(buf, tokens[1]);
      strcat(buf, " X");
      gtk_label_set_text(GTK_LABEL(kolgracz[index].label), buf);
#ifdef DEBUG
      fprintf(stderr, "PLAYER_ELIMINATED command accepted\n");
#endif
      return 1;
    }
    else return 0; 
  }
#ifdef DEBUG
  else
    fprintf(stderr, "Unknown command\n");
#endif
  return 0;
}

/*EOF*/
