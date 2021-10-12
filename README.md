# Screen worms
Client and server for a simple curvefever-like game made as an assignment for Computer Networks class at University of Warsaw.

To start the server run
`./screen-worms-server [-p n] [-s n] [-t n] [-v n] [-w n] [-h n]`
where
 * `-p n` – port number (2021 by default)
 * `-s n` – seed for rng (time(NULL) by default)
 * `-t n` – turn speed (integer, 6 by default) 
 * `-v n` – game speed (integer, 50 by default)
 * `-w n` – board width in pixels (640 by default)  
 * `-h n` – board height in pixels (480 by default) 

To start the client run
`./screen-worms-client game_server [-n player_name] [-p n] [-i gui_server] [-r n]`
where  
* `-n player_name` – alphanumeric string, if not provided it joins the game as a spectator
* `-p n` – game server's port (2021 by default)
* `-i n` – gui server's address (localhost by default)
* `-r n` – gui server's port (20210 by default)

To run the GUI use `./gui2 [port]`

