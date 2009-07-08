/* libmpdclient
   (c) 2003-2009 The Music Player Daemon Project
   This project's homepage is: http://www.musicpd.org

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <mpd/recv.h>
#include <mpd/pair.h>
#include <mpd/parser.h>
#include "internal.h"
#include "sync.h"

#include <string.h>

struct mpd_pair *
mpd_recv_pair(struct mpd_connection *connection)
{
	struct mpd_pair *pair;
	char *line;
	enum mpd_parser_result result;
	const char *msg;

	assert(connection != NULL);

	if (mpd_error_is_defined(&connection->error))
		return NULL;

	if (connection->pair != PAIR_NONE) {
		/* dequeue the pair from mpd_enqueue_pair() */
		pair = connection->pair;
		connection->pair = PAIR_NONE;
		return pair;
	}


	if (!connection->receiving ||
	    (connection->sending_command_list &&
	     connection->command_list_remaining > 0 &&
	     connection->discrete_finished)) {
		mpd_error_code(&connection->error, MPD_ERROR_STATE);
		mpd_error_message(&connection->error,
				  "already done processing current command");
		return NULL;
	}

	line = mpd_sync_recv_line(connection->async, &connection->timeout);
	if (line == NULL)
		return NULL;

	result = mpd_parser_feed(connection->parser, line);
	switch (result) {
	case MPD_PARSER_MALFORMED:
		mpd_error_code(&connection->error, MPD_ERROR_MALFORMED);
		mpd_error_printf(&connection->error,
				 "Failed to parse MPD response");
		return NULL;

	case MPD_PARSER_SUCCESS:
		if (!mpd_parser_is_discrete(connection->parser)) {
			if (connection->sending_command_list &&
			    connection->command_list_remaining > 0) {
				mpd_error_code(&connection->error,
					       MPD_ERROR_MALFORMED);
				mpd_error_message(&connection->error,
						  "expected more list_OK's");
				connection->command_list_remaining = 0;
			}

			connection->receiving = false;
			connection->discrete_finished = false;
		} else {
			if (!connection->sending_command_list ||
			    connection->command_list_remaining == 0) {
				mpd_error_code(&connection->error,
					       MPD_ERROR_MALFORMED);
				mpd_error_message(&connection->error,
						  "got an unexpected list_OK");
			} else {
				connection->discrete_finished = true;
				--connection->command_list_remaining;
			}
		}

		return NULL;

	case MPD_PARSER_ERROR:
		connection->receiving = false;
		mpd_error_ack(&connection->error,
			      mpd_parser_get_ack(connection->parser),
			      mpd_parser_get_at(connection->parser));
		msg = mpd_parser_get_message(connection->parser);
		if (msg == NULL)
			msg = "Unspecified MPD error";
		mpd_error_message(&connection->error, msg);
		return NULL;

	case MPD_PARSER_PAIR:
		pair = mpd_pair_new(mpd_parser_get_name(connection->parser),
				    mpd_parser_get_value(connection->parser));
		if (pair == NULL)
			mpd_error_code(&connection->error, MPD_ERROR_OOM);
		return pair;
	}

	/* unreachable */
	assert(false);
	return NULL;
}

struct mpd_pair *
mpd_recv_pair_named(struct mpd_connection *connection, const char *name)
{
	struct mpd_pair *pair;

	while ((pair = mpd_recv_pair(connection)) != NULL) {
		if (strcmp(pair->name, name) == 0)
			return pair;

		mpd_pair_free(pair);
	}

	return NULL;
}

char *
mpd_recv_value_named(struct mpd_connection *connection, const char *name)
{
	struct mpd_pair *pair;
	char *value;

	pair = mpd_recv_pair_named(connection, name);
	if (pair == NULL)
		return NULL;

	value = strdup(pair->value);
	mpd_pair_free(pair);

	if (value == NULL)
		mpd_error_code(&connection->error, MPD_ERROR_OOM);

	return value;
}

void
mpd_enqueue_pair(struct mpd_connection *connection, struct mpd_pair *pair)
{
	assert(connection != NULL);
	assert(pair == NULL || (pair->name != NULL && pair->value != NULL));
	assert(connection->pair == PAIR_NONE);

	connection->pair = pair;
}