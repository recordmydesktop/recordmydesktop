/******************************************************************************
*                            recordMyDesktop                                  *
*******************************************************************************
*                                                                             *
*            Copyright (C) 2006,2007,2008 John Varouhakis                     *
*                                                                             *
*                                                                             *
*   This program is free software; you can redistribute it and/or modify      *
*   it under the terms of the GNU General Public License as published by      *
*   the Free Software Foundation; either version 2 of the License, or         *
*   (at your option) any later version.                                       *
*                                                                             *
*   This program is distributed in the hope that it will be useful,           *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of            *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
*   GNU General Public License for more details.                              *
*                                                                             *
*   You should have received a copy of the GNU General Public License         *
*   along with this program; if not, write to the Free Software               *
*   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA  *
*                                                                             *
*                                                                             *
*                                                                             *
*   For further information contact me at johnvarouhakis@gmail.com            *
******************************************************************************/

#include "config.h"
#include "rmd_jack.h"

#include "rmd_types.h"

#include <pthread.h>

#include <string.h>

#ifdef HAVE_LIBJACK


/**
*   Callback for capture through jack
*
*   \param  nframes Number of frames captured
*
*   \param jdata_t  Pointer to JackData struct containing port
*				   and client information
*
*   \returns Zero always
*/
static int rmdJackCapture(jack_nframes_t nframes, void *jdata_t)
{
	JackData *jdata = (JackData *)jdata_t;

	if (!jdata->pdata->running || jdata->pdata->paused || !jdata->capture_started) {
		/* still produce a signal on sound_data_read just to ensure the other side
		 * wakes up and realizes running/paused etc.
		 */
		pthread_cond_signal(jdata->sound_data_read);

		return 0;
	}

	for (int i = 0; i < jdata->nports; i++)
		jdata->portbuf[i] = jack_port_get_buffer(jdata->ports[i], nframes);

	pthread_mutex_lock(&jdata->pdata->avd_mutex);
	jdata->pdata->avd -= jdata->pdata->periodtime_us;
	pthread_mutex_unlock(&jdata->pdata->avd_mutex);

	pthread_mutex_lock(jdata->sound_buffer_mutex);
//vorbis analysis buffer wants uninterleaved data
//so we are simply placing the buffers for every channel
//sequentially on the ringbuffer
	for (int i = 0; i < jdata->nports; i++)
		jack_ringbuffer_write(	jdata->sound_buffer,
					(void *)(jdata->portbuf[i]),
					nframes *
					sizeof(jack_default_audio_sample_t));

	pthread_cond_signal(jdata->sound_data_read);
	pthread_mutex_unlock(jdata->sound_buffer_mutex);

	return 0;
}

/**
*   Register and Activate specified ports
*
*   \param jdata_t  Pointer to JackData struct containing port
*				   and client information
*
*   \returns 0 on Success, 1 on failure
*/
static int rmdSetupPorts(JackData *jdata)
{
	jdata->ports = malloc(sizeof(jack_port_t *) * jdata->nports);
	jdata->portbuf = malloc(sizeof(jack_default_audio_sample_t *) * jdata->nports);
	memset(jdata->portbuf, 0, sizeof(jack_default_audio_sample_t *) * jdata->nports);

	for (int i = 0; i < jdata->nports; i++) {
		char name[64];

		snprintf(name, sizeof(name), "input_%d", i + 1);

		jdata->ports[i] = jack_port_register(	jdata->client,
							name,
							JACK_DEFAULT_AUDIO_TYPE,
							JackPortIsInput,
							0);

		if (!jdata->ports[i]) {
			fprintf(stderr, "Cannot register input port \"%s\"!\n", name);
			return 1;
		}

		if (jack_connect(jdata->client, jdata->port_names[i], jack_port_name(jdata->ports[i]))) {

			fprintf(stderr,	"Cannot connect input port %s to %s\n",
					jack_port_name(jdata->ports[i]),
					jdata->port_names[i]);
			return 1;
		}
	}
	return 0;
}

//in case the jack server shuts down
//the program should stop recording,
//encode the result(if not on the fly)
//an exit cleanly.
static void rmdJackShutdown(void *jdata_t)
{
	JackData *jdata = (JackData *)jdata_t;

	pthread_mutex_unlock(jdata->sound_buffer_mutex);
	jdata->pdata->running = FALSE;
	pthread_cond_signal(jdata->sound_data_read);
	pthread_mutex_lock(jdata->sound_buffer_mutex);

	fprintf (stderr, "JACK shutdown\n");
}

int rmdStartJackClient(JackData *jdata)
{
	float ring_buffer_size = 0.0;
	int pid;
	char pidbuf[8];
	char rmd_client_name[32];

	//construct the jack client name
	//which is recordMyDesktop-pid
	//in order to allow multiple
	//instances of recordMyDesktop
	//to connetc to a Jack Server
	strcpy(rmd_client_name, "recordMyDesktop-");
	pid = getpid();
	snprintf( pidbuf, 8, "%d", pid );
	strcat(rmd_client_name, pidbuf);

	jack_options_t options = JackUseExactName;

	if (getenv ("JACK_START_SERVER") == NULL) {
		options |= JackNoStartServer;
	}

	if ((jdata->client = jack_client_open(rmd_client_name, options, NULL)) == 0) {
		fprintf(stderr,	"Could not create new client!\n"
				"Make sure that Jack server is running!\n");
		return 15;
	}
//in contrast to ALSA and OSS, Jack dictates frequency
//and buffersize to the values it was launched with.
//Supposedly jack_set_buffer_size can set the buffersize
//but that causes some kind of halt and keeps giving me
//zero buffers.
//recordMyDesktop cannot handle buffer size changes.
//FIXME
//There is a callback for buffer size changes that I should use.
//It will provide clean exits, instead of ?segfaults? .
//Continuing though is not possible, with the current layout
//(it might be in some cases, but it will certainly be the cause
//of unpredicted problems). A clean exit is preferable
//and any recording up to that point will be encoded and saved.
	jdata->frequency = jack_get_sample_rate(jdata->client);
	jdata->buffersize = jack_get_buffer_size(jdata->client);
	ring_buffer_size = (	jdata->ringbuffer_secs*
				jdata->frequency*
				sizeof(jack_default_audio_sample_t)*
				jdata->nports);
	jdata->sound_buffer = jack_ringbuffer_create((int)(ring_buffer_size+0.5));//round up
	jack_set_process_callback(jdata->client, rmdJackCapture, jdata);
	jack_on_shutdown(jdata->client, rmdJackShutdown, jdata);

	if (jack_activate(jdata->client)) {
		fprintf(stderr, "cannot activate client!\n");
		return 16;
	}

	if (rmdSetupPorts(jdata)) {
		jack_client_close(jdata->client);
		return 17;
	}

	return 0;
}

int rmdStopJackClient(JackData *jdata)
{
	int ret = 0;

	jack_ringbuffer_free(jdata->sound_buffer);
	if (jack_client_close(jdata->client)) {
		fprintf(stderr, "Cannot close Jack client!\n");
		ret = 1;
	}

/*TODO*/
//I need to make some kind of program/thread
//flow diagram to see where it's safe to dlclose
//because here it causes a segfault.

//	 if (dlclose(jdata->jack_lib_handle)) {
//		 fprintf(stderr, "Cannot unload Jack library!\n");
//		 ret=1;
//	 }
//	 else fprintf(stderr, "Unloaded Jack library.\n");

	return ret;
}

#endif
