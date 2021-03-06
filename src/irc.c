#include "irc.h"

// Lovingly used from
// https://mybuddymichael.com/writings/a-regular-expression-for-irc-messages.html
// (slightly modified)
static const char *irc_regex = "^(?:[:](\\S+) )?(\\S+)(?: (?!:)(.+?))?(?: (?!:)(.+?))?(?: [:](.*?))?\\R?$";

// Channels names are strings (beginning with a '&' or '#' character)
// of length up to 200 characters.
// (https://tools.ietf.org/html/rfc1459#section-1.3)
static char current_channel[256];
static char nick[16];
static char user[16];
static char real[16];

// List of channels currently connected to
static char **channels;
static int csize = 0; // Number of channels in channels array

static char *server = "";
static int keep_logs = 0;

int irc_init(char *serv, int log, char *nck, char *usr, char *rl) {
	server    = serv;
	keep_logs = log;

	strncpy(nick, nck, sizeof(nick));
	strncpy(user, usr, sizeof(user));
	strncpy(real, rl,  sizeof(real));

	if (strlen(nick) > 9) {
		printf("Nickname cannot be longer than 9 characters\n");
		return 0;
	}
	if (strlen(user) > 9) {
		printf("Username cannot be longer than 9 characters\n");
		return 0;
	}
	if (strlen(real) > 9) {
		printf("Real name cannot be longer than 9 characters\n");
		return 0;
	}

	char nick_msg[512];
	snprintf(nick_msg, sizeof(nick_msg), "NICK %s\r\n", nick);
	write_socket(nick_msg);

	char user_msg[512];
	snprintf(user_msg, sizeof(user_msg), "USER %s 0 * :%s\r\n", user, real);
	write_socket(user_msg);

	return 1;
}

int irc_receive(char *buffer, int R) {
	char **output;
	char *prefix, *type, *dest, *middle, *msg;
	int print = 1;
	const char *color = "";

	// More re_match
	char **_output;
	int _r = 0;

	int log = 0;
	FILE *lp;

	char **h_output;
	int hr;
	char *host = "";
	char *hostname  = "";
	char temp[1024];

	int retval = 1;

	int r = re_match(buffer, irc_regex, &output, 0);
	if (r > 3) { // Must at least have dest, which is third match
		prefix = output[1];
		type   = output[2];
		dest   = output[3];
		middle = (r > 4) ? output[4] : "";
		msg    = (r > 5) ? output[5] : "";

///////////////////////
		// rl_printf("match: %s\n", output[0]);
///////////////////////
		// rl_printf("prefix: %s\n", prefix);
		// rl_printf("type: %s\n", type);
		// rl_printf("dest: %s\n", dest);
		// rl_printf("middle: %s\n", middle);
		// rl_printf("msg: %s\n", msg);
///////////////////////

		hr = re_match(prefix, "^([^!]+)!(.+)$", &h_output, 0);
		if (hr == 3) {
			host     = h_output[1];
			hostname = h_output[2];
		}

		// Check if type is a number
		char *typenoptr;
		int typeno = strtol(type, &typenoptr, 10);


		// Reply to ping messages to stay connected to server
		if (strcmp(type, "PING") == 0) {
			print = 0;
			char pong[512];
			snprintf(pong, sizeof(pong), "PONG :%s\r\n", msg);
			write_socket(pong);
		}


		////////  Different colors  ////////
		if (strcmp(dest, "*") == 0 || strcmp(dest, "AUTH") == 0) {
			color = "magenta";
		}
		else if (strcmp(dest, nick) == 0) {
			color = "cyan";
		}
		// Channels names are strings (beginning with a '&' or '#' character)
		// (https://tools.ietf.org/html/rfc1459#section-1.3)
		else if (dest[0] == '#' || dest[0] == '&') {
			if (strcmp(dest, current_channel) != 0) {
				print = 0;
			}
			// Log messages from channel
			log = 1;
		}


		////////  Special action messages  ////////
		//         (different colors)
		if (strcmp(type, "JOIN") == 0) {
			if (!*dest) dest = msg; // Some servers use msg instead of dest
			if (strcmp(dest, current_channel) == 0) print = 1;
			if (strcmp(host, nick) == 0) {
				// Switch to alternate screen buffer
				galt()
					? printf("%s%s", xget("rmcup"), xget("smcup"))
					: printf("%s",   xget("smcup"));
				fflush(stdout);
				alt(1);

				// Check if previous log exists
				char *lname = malloc(256); // Filenames can only be 255 chars
				setup_log(&lname, 256, dest);
				// If log exists, print to screen
				if (access(lname, F_OK) != -1) {
					lp = fopen(lname, "rb+");
					if (!lp)
						error("Error opening file for reading");

					// Print log file
					int c;
					while ((c = getc(lp)) != EOF)
						putchar(c);

					fclose(lp);
				}
				free(lname);

				snprintf(temp, sizeof(temp), "Now talking on %s", dest);

				// Set current channel
				memset(current_channel, 0, sizeof(current_channel));
				strncpy(current_channel, dest, sizeof(current_channel));

				// Add channel to list of channels
				channels = csize ? realloc(channels, ++csize * sizeof(char *))
				                 : malloc(++csize * sizeof(char *));

				char destcpy[256];
				channels[csize-1] = malloc(sizeof(destcpy));
				strncpy(destcpy, dest, sizeof(destcpy));
				memcpy(channels[csize-1], destcpy, sizeof(destcpy));
			}
			else
				snprintf(temp, sizeof(temp), "%s [%s] has joined %s", host, hostname, dest);
			msg = temp;
			log = 1;
			color = "green";
		}
		else if (strcmp(type, "PART") == 0) {
			if (!*dest) dest = msg; // Some servers use msg instead of dest
			if (strcmp(dest, current_channel) == 0) print = 1;
			if (strcmp(host, nick) == 0) {
				snprintf(temp, sizeof(temp), "Left channel %s", dest);
				if (strcmp(dest, current_channel) == 0)
					memset(current_channel, 0, sizeof(current_channel));

				// Remove channel from list of channels
				int mark = -1;
				for (int i = 0; i < csize; i++) {
					if (strcmp(channels[i], dest) == 0) {
						mark = i;
					}
					if (mark >= 0)
						channels[i] = i+1 < csize ? channels[i+1] : 0;
				}

				free(channels[csize-1]);
				channels = realloc(channels, --csize * sizeof(char *));

				printf("%s", xget("rmcup")); // Switch back to normal screen
				fflush(stdout);
				alt(0);
			}
			else {
				snprintf(temp, sizeof(temp), "%s [%s] has left %s", host, hostname, dest);
			}

			msg = temp;
			log = 1;
			color = "yellow";
		}
		else if (strcmp(type, "PRIVMSG") == 0) {
			if (strcmp(dest, nick) == 0) {
///////////////////////
				// rl_printf("prefix: %s\n", prefix);
				// rl_printf("type: %s\n", type);
				// rl_printf("dest: %s\n", dest);
				// rl_printf("middle: %s\n", middle);
				// rl_printf("msg: %s\n", msg);
///////////////////////
				// // // // // // // // // //
				// // // // // // // // // //
				// Handle later when fully implement `/msg`
				// If host is in the list of users, it's a privmsg, start new "channel" for
				//     privmsg with user
				// Might not be in list of users: e.g., py-ctcp!ctcp@ctcp-scanner.rizon.net
				// Special cases for nickserv and chanserv??
				// Change cursor prompt to red to notify user that someone is chatting them
				// // // // // // // // // //
			}
			else {
				int freehost = 0;
				// Someone is talking about user, but not self
				if (strstr(msg, nick) != NULL && strcmp(host, nick) != 0) {
					host = scolor(host, "red");
					freehost = 1;
				}

				//// Display action messages
				_r = re_match(msg, "^\1ACTION (.+)\1$", &_output, 0);
				if (_r == 2) {
					msg = _output[1];
					snprintf(temp, sizeof(temp), "* %s %s", host, msg);
				}
				else {
					host = scolor(host, "cyan");
					snprintf(temp, sizeof(temp), "<%s> %s", host, msg);
					freehost = 1;
				}

				if (freehost)
					free(host);

				log = 1;
				msg = temp;
			}
		}
		else if (strcmp(type, "QUIT") == 0) {
			snprintf(temp, sizeof(temp), "%s [%s] has quit [%s]", host, hostname, msg);
			msg = temp;
			log = 1;
			color = "yellow";
		}


		////////  Check middle for messages about channel  ////////
		char *dfree;
		int dsize = 0;
		if (
			(strcmp(type, "PRIVMSG") == 0 || (
				!*typenoptr && (
					typeno == 331 ||
					typeno == 332 ||
					typeno == 353 ||
					typeno == 366
				)
			)) && *middle
		) {
			char *mcpy, *tofree;
			tofree = mcpy = malloc(strlen(middle)+1);
			strncpy(mcpy, middle,  strlen(middle)+1);
			char *token;
			while ( (token = strsep(&mcpy, " ")) ) {
				if (token[0] == '#' || token[0] == '&') {
					if (strcmp(token, current_channel) != 0) {
						print = 0;
					}
					dsize = strlen(token)+1;
					dfree = dest = malloc(dsize);
					strncpy(dest, token, dsize);
					// Log messages from channel
					log = 1;
					break;
				}
			}
			free(tofree);
		}
		else if (strcmp(type, "NOTICE") == 0) {
			_r = re_match(msg, "^.*?\\[([#&].+)\\].*?$", &_output, 0);
			if (_r == 2) {
				char *token = _output[1];
				if (strcmp(token, current_channel) != 0) {
					print = 0;
				}
				dsize = strlen(token)+1;
				dfree = dest = malloc(dsize);
				strncpy(dest, token, dsize);
				// Log messages from channel
				log = 1;
			}
		}


		////////  Log setup  ////////
		if (log) {
			char *lname = malloc(256); // Filenames can only be 255 chars
			setup_log(&lname, 256, dest);
			lp = fopen(lname, "ab+");
			free(lname);
			if (!lp)
				error("Error opening file for writing");
		}
		if (dsize > 0)
			free(dfree);


		////////  Printing & logging  ////////
		time_t rawtime;
		struct tm *timeinfo;

		time(&rawtime);
		timeinfo = localtime(&rawtime);
		char out_time[8];
		snprintf(out_time, sizeof(out_time), "[%02d:%02d]", timeinfo->tm_hour, timeinfo->tm_min);

		// Color output
		char *c_time = scolor(out_time, "blue");
		if (*color) {
			if (*middle) middle = scolor(middle, color);
			if (*msg)    msg    = scolor(msg,    color);
		}

		if (*middle) {
			if (*msg) {
				if (log)       fprintf(lp, "%s %s :%s\n", c_time, middle, msg);
				if (print) R ? rl_printf(  "%s %s :%s\n", c_time, middle, msg)
				             : printf(     "%s %s :%s\n", c_time, middle, msg);
			}
			else {
				if (log)       fprintf(lp, "%s %s\n", c_time, middle);
				if (print) R ? rl_printf(  "%s %s\n", c_time, middle)
				             : printf(     "%s %s\n", c_time, middle);
			}
		}
		else if (*msg) {
			if (log)       fprintf(lp, "%s %s\n", c_time, msg);
			if (print) R ? rl_printf(  "%s %s\n", c_time, msg)
			             : printf(     "%s %s\n", c_time, msg);
		}

		// Free color variables
		free(c_time);
		if (*color) {
			if (*middle) free(middle);
			if (*msg)    free(msg);
		}
	}
	else {
		destroy_prompt();
		rl_printf("Error parsing message from irc server\n");
		retval = 0;
	}

	if (r > 0) {
		// Free output
		for (int i = 0; i < r; i++)
			free(output[i]);
		free(output);
	}

	if (_r > 0) {
		// Free output
		for (int i = 0; i < _r; i++)
			free(_output[i]);
		free(_output);
	}

	if (hr > 0) {
		// Free h_output
		for (int i = 0; i < hr; i++)
			free(h_output[i]);
		free(h_output);
	}

	if (log) {
		fclose(lp);
	}

	return retval;
}

int irc_send(char *buffer) {
	char send[512];


	////////  No command, just send privmsg  ////////
	if (buffer[0] != '/') {
		if (*current_channel) {
			// Split up long messages and send them in parts
			// Determine the max size of each part
			snprintf(send, sizeof(send), ": PRIVMSG %s :\r\n", current_channel);
			int psize = sizeof(send) - ( strlen(send) + 32 ); // Extra padding for hostmask
			memset(send, 0, sizeof(send));

			char part[psize];
			int len = 0;

			while (*buffer) {
				// If this is the second+ loop, pause between sending (avoid spamming server)
				if (len) sleep(1);

				// Copy part of the message
				strncpy(part, buffer, psize);
				part[psize] = 0;


				// Send the message to current channel
				snprintf(send, sizeof(send), "PRIVMSG %s :%s\r\n", current_channel, part);
				write_socket(send);

				// Print the message because the server doesn't send it back
				memset(send, 0, sizeof(send));
				snprintf(send, sizeof(send), ":%s!X PRIVMSG %s :%s\r\n", nick, current_channel, part);
				irc_receive(send, 0);


				len = strlen(buffer);
				buffer += len > psize ? psize : len; // Advance the pointer
			}
		}
		else {
			printf("No channel joined. Try /join #<channel>\n");
		}
		return 1;
	}

	// Command
	char **output;
	char *dest, *msg;
	int r = 0;

	int retval = 1;

	buffer++; // Remove '/'

	char *buffcpy, *tofree;
	tofree = buffcpy = malloc(512);
	strncpy(buffcpy, buffer, 512);
	char *command;

	command = strsep(&buffcpy, " ");
	for (int i = 0; command[i]; i++)
		command[i] = tolower(command[i]);


	////////  Command parsing  ////////
	if (strcmp(command, "help") == 0 || strcmp(command, "h") == 0) {
		printf("\
Supported commands:\n\
\n\
/channel <channel>     Switches the current channel\n\
/channels              Lists the channels currently connected to\n\
/help                  Shows this help message\n\
/join <channel>        Joins a channel\n\
/list                  Lists the channels on the server\n\
/me <action>           Sends the action to the current channel\n\
/msg <user> <message>  Sends a private message to a user\n\
/names                 Lists the users in the current channel\n\
/part [<channel>]      Leaves a specified channel\n\
/quit [<message>]      Quits, sending a specified message\n\
\n\
Shortcuts:\n\
/(c)hannel\n\
/(h)elp\n\
/(j)oin\n\
/(m)sg\n\
/(p)art\n\
/(q)uit\n\
");
	}
	else if (strcmp(command, "join") == 0 || strcmp(command, "j") == 0) {
		r = re_match(buffer, "^(join|j) (.+)$", &output, 1);
		if (r == 3) {
			dest = output[2];
			snprintf(send, sizeof(send), "JOIN %s\r\n", dest);
			write_socket(send);
		}
		else {
			printf("Usage: /join <channel>, Joins a channel\n");
		}
	}
	else if (strcmp(command, "part") == 0 || strcmp(command, "p") == 0) {
		r = re_match(buffer, "^(part|p) (.+)$", &output, 1);
		if (r == 3) {
			dest = output[2];
			snprintf(send, sizeof(send), "PART %s\r\n", dest);
			write_socket(send);
		}
		else if (*current_channel) {
			snprintf(send, sizeof(send), "PART %s\r\n", current_channel);
			write_socket(send);
		}
		else {
			printf("No channel joined. Try /join #<channel>\n");
		}
	}
	else if (strcmp(command, "quit") == 0 || strcmp(command, "q") == 0) {
		r = re_match(buffer, "^(quit|q) (.+)$", &output, 1);
		if (r == 3) {
			msg = output[2];
			snprintf(send, sizeof(send), "QUIT :%s\r\n", msg);
			write_socket(send);
		}
		else {
			snprintf(send, sizeof(send), "QUIT\r\n");
			write_socket(send);
		}
		retval = 0;
	}
	else if (strcmp(command, "msg") == 0 || strcmp(command, "m") == 0) {
		if (!*current_channel) {
			printf("No channel joined. Try /join #<channel>\n");
		}
		else {
			r = re_match(buffer, "^(msg|m) (\\S+) (.+)$", &output, 1);

			if (r == 4) {
				dest = output[2];
				msg  = output[3];

				snprintf(send, sizeof(send), "PRIVMSG %s :%s\r\n", dest, msg);
				write_socket(send);
			}
			else {
				printf("Usage: /msg <user> <message>, Sends a private message to a user\n");
			}
		}
	}
	else if (strcmp(command, "me") == 0) {
		if (!*current_channel) {
			printf("No channel joined. Try /join #<channel>\n");
		}
		else {
			r = re_match(buffer, "^(me) (.+)$", &output, 1);

			if (r == 3) {
				msg  = output[2];

				snprintf(send, sizeof(send), "PRIVMSG %s :\1ACTION %s\1\r\n", current_channel, msg);
				write_socket(send);

				// Print the message because the server doesn't send it back
				memset(send, 0, sizeof(memset));
				snprintf(send, sizeof(send),
					":%s!X PRIVMSG %s :\1ACTION %s\1\r\n", nick, current_channel, msg);
				irc_receive(send, 0);
			}
			else {
				printf("Usage: /me <action>, Sends the action to the current channel\n");
			}
		}
	}
	else if (strcmp(command, "names") == 0) {
		if (*current_channel) {
			snprintf(send, sizeof(send), "NAMES %s\r\n", current_channel);
			write_socket(send);
		}
		else {
			printf("No channel joined. Try /join #<channel>\n");
		}
	}
	else if (strcmp(command, "list") == 0) {
		snprintf(send, sizeof(send), "LIST\r\n");
		write_socket(send);
	}
	else if (strcmp(command, "channel") == 0 || strcmp(command, "c") == 0) {
		if (!csize) {
			printf("No channel joined. Try /join #<channel>\n");
		}
		else {
			r = re_match(buffer, "^(channel|c) (\\S+)$", &output, 1);

			if (r == 3) {
				dest = output[2];

				// Check if in list of channels
				int conn_to = 0;
				for (int i = 0; i < csize; i++) {
					if (strcmp(channels[i], dest) == 0){
						conn_to = 1;
						break;
					}
				}

				if (conn_to) {
					// Switch to alternate screen buffer
					galt()
						? printf("%s%s", xget("rmcup"), xget("smcup"))
						: printf("%s",   xget("smcup"));
					fflush(stdout);
					alt(1);

					memset(current_channel, 0, sizeof(current_channel));
					strncpy(current_channel, dest, sizeof(current_channel));

					FILE *lp;
					char *lname = malloc(256); // Filenames can only be 255 chars
					setup_log(&lname, 256, dest);
					lp = fopen(lname, "rb+");
					free(lname);
					if (!lp)
						error("Error opening file for reading");

					// Print log file
					int c;
					while ((c = getc(lp)) != EOF)
						putchar(c);

					fclose(lp);
				}
				else {
					printf("Not connected to channel: %s\n", dest);
				}
			}
			else {
				printf("Usage: /channel <channel>, Switches the current channel\n");
			}
		}
	}
	else if (strcmp(command, "channels") == 0) {
		printf(
			csize ? "Connected to:\n" : "No channel joined. Try /join #<channel>\n"
		);
		for (int i = 0; i < csize; i++)
			printf("%s\n", channels[i]);
	}
	else {
		printf("%s :Unknown command\n", command);
	}

	free(tofree);
	if (r > 0) {
		// Free output
		for (int i = 0; i < r; i++)
			free(output[i]);
		free(output);
	}

	return retval;
}

void irc_clean() {
	// Free channels
	for (int i = 0; i < csize; i++)
		free(channels[i]);

	// Delete logs
	glob_t paths;
	if (glob(".IRC_*.log", GLOB_NOSORT, NULL, &paths) == 0) {
		char *lname;
		// Loop through each file matching pattern
		for (size_t i = 0; i < paths.gl_pathc; i++) {
			lname = paths.gl_pathv[i];
			// Remove logs
			if (!keep_logs) {
				remove(lname);
			}
			else {
				FILE *lp;
				lp = fopen(lname, "ab+");
				if (!lp)
					error("Error opening file for writing");

				// Write date stamp to log for future reading
				time_t rawtime;
				struct tm *timeinfo;

				time(&rawtime);
				timeinfo = localtime(&rawtime);
				// Write previous log
				fprintf(lp, "-------- PREVIOUS LOG [%02d/%02d] --------\n",
					timeinfo->tm_mon+1, // Counts months from January, so +1
					timeinfo->tm_mday);
				fclose(lp);
			}
		}
		globfree(&paths);
	}
}

void setup_log(char **lname, int lsize, char *dest) {
	int thsize = strlen(server) + strlen(dest) + 2; // 1 for ':' and 1 for '\0'
	char tohash[thsize];
	snprintf(tohash, thsize, "%s:%s", server, dest);
	uint32_t hash = murmur3_32(tohash, thsize, 0x1337);
	snprintf(*lname, lsize, ".IRC_%u.log", hash);
}
