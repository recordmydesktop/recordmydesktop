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
#include "rmd_cache.h"

#include "rmd_specsfile.h"
#include "rmd_threads.h"
#include "rmd_types.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CACHE_FILE_SIZE_LIMIT (500 * 1024 * 1024)


/* periodic fdatasync thread for cache writers when fdatasyncing is enabled */
static void * rmdCacheFileSyncer(CacheFile *file)
{
	struct timespec	delay;
	int		fd;

	assert(file);
	assert(file->periodic_datasync_ms);

	rmdThreadsSetName("rmdCacheSyncer");

	delay.tv_sec = file->periodic_datasync_ms / 1000;
	delay.tv_nsec = (file->periodic_datasync_ms - delay.tv_sec * 1000) * 1000000;

	if (file->gzfp)
		fd = file->gzfd;
	else
		fd = fileno(file->fp);

	for (;;) {
		nanosleep(&delay, NULL);
		fdatasync(fd);
	}

	return NULL;
}


/* open file @ path storing the file handles in *file,
 * this doesn't store the new path since it's primarily
 * for the purposes of opening new chapters for the same
 * base path.
 */
static int _rmdCacheFileOpen(CacheFile *file, const char *path)
{
	const char	*modestr = "rb";
	int		flags = O_RDONLY;

	assert(file);

	if (file->mode == RMD_CACHE_FILE_MODE_WRITE) {
		flags = O_CREAT|O_WRONLY;
		modestr = "wb";

		if (file->compressed)
			modestr = "wb0f";
	}

	if (file->compressed) {
		/* zlib doesn't expose a fileno() equivalent for the syncer */
		file->gzfd = open(path, flags, S_IRUSR|S_IWUSR);
		if (file->gzfd < 0)
			return -1;

		file->gzfp = gzdopen(file->gzfd, modestr);
	} else {
		file->fp = fopen(path, modestr);
	}

	if (!file->gzfp && !file->fp)
		return -1;

	file->chapter_n_bytes = 0;

	if (file->mode == RMD_CACHE_FILE_MODE_WRITE && file->periodic_datasync_ms)
		pthread_create(&file->syncer_thread, NULL, (void *(*)(void *))rmdCacheFileSyncer, file);

	return 0;
}


/* only close the internal file handle, but don't free file */
static int _rmdCacheFileClose(CacheFile *file)
{
	assert(file);

	if (file->mode == RMD_CACHE_FILE_MODE_WRITE && file->periodic_datasync_ms) {
		pthread_cancel(file->syncer_thread);
		pthread_join(file->syncer_thread, NULL);
	}

	/* TODO: return meaningful -errno on errors? */
	if (file->gzfp) {
		if (gzclose(file->gzfp) != Z_OK)
			return -1;

		file->gzfp = NULL;
	} else if (file->fp) {
		if (fclose(file->fp))
			return -1;

		file->fp = NULL;
	}

	return 0;
}


/* open a CacheFile @ path, in the specified mode, returns NULL on error */
CacheFile * rmdCacheFileOpen(ProgData *pdata, const char *path, CacheFileMode mode)
{
	CacheFile	*f;

	assert(pdata);
	assert(path);

	f = calloc(1, sizeof(*f));
	if (!f)
		return NULL;

	f->path = strdup(path);
	if (!f->path) {
		free(f);
		return NULL;
	}

	f->mode = mode;
	f->compressed = !pdata->args.zerocompression;
	f->periodic_datasync_ms = pdata->args.periodic_datasync_ms;

	if (_rmdCacheFileOpen(f, path) < 0)
		return NULL;

	return f;
}


/* close a CacheFile, returns < 0 on error */
int rmdCacheFileClose(CacheFile *file)
{
	assert(file);

	if (_rmdCacheFileClose(file) < 0)
		return -1;

	free(file->path);
	free(file);

	return 0;
}


/**
*Construct an number postfixed name
*
* \param name base name
*
* \param newname modified name
*
* \n number to be used as a postfix
*
*/
static void _rmdCacheFileN(char *name, char **newname, int n) // Nth cache file
{
	char numbuf[8];

	strcpy(*newname, name);
	strcat(*newname, ".");
	snprintf(numbuf, 8, "%d", n);
	strcat(*newname, numbuf);
}


/* read from a CacheFile, identical to gzread() */
ssize_t rmdCacheFileRead(CacheFile *file, void *ptr, size_t len)
{
	ssize_t	ret, read_n_bytes = 0;

	assert(file);
	assert(ptr);

	assert(file->mode == RMD_CACHE_FILE_MODE_READ);

retry:
	if (file->gzfp) {
		int	r;

		r = gzread(file->gzfp, ptr + read_n_bytes, len);
		if (r < 0)
			return -1;

		ret = r;
	} else {
		ret = fread(ptr + read_n_bytes, 1, len, file->fp);
	}

	read_n_bytes += ret;
	file->chapter_n_bytes += read_n_bytes;
	file->total_n_bytes += read_n_bytes;

	if (ret < len) {
		char *newpath = malloc(strlen(file->path) + 10);

		len -= ret;

		if (_rmdCacheFileClose(file) < 0) {
			free(newpath);
			return -1;
		}

		/* look for next chapter */
		_rmdCacheFileN(file->path, &newpath, file->chapter + 1);
		if (_rmdCacheFileOpen(file, newpath) == 0) {
			file->chapter++;
			free(newpath);
			goto retry;
		}

		free(newpath);
	}

	return read_n_bytes;
}


/* write to a CacheFile, identical to gzwrite() */
ssize_t rmdCacheFileWrite(CacheFile *file, const void *ptr, size_t len)
{
	ssize_t	ret;

	assert(file);
	assert(ptr);

	assert(file->mode == RMD_CACHE_FILE_MODE_WRITE);

	/* transparently open next chapter if needed */
	if (file->chapter_n_bytes > CACHE_FILE_SIZE_LIMIT) {
		char *newpath = malloc(strlen(file->path) + 10);

		if (_rmdCacheFileClose(file) < 0)
			return -1;

		file->chapter++;

		_rmdCacheFileN(file->path, &newpath, file->chapter);
		if (_rmdCacheFileOpen(file, newpath) < 0) {
			free(newpath);
			return -1;
		}
	}

	if (file->gzfp) {
		int	r;

		r = gzwrite(file->gzfp, ptr, len);
		if (r < 0)
			return -1;

		ret = r;
	} else {
		ret = fwrite(ptr, 1, len, file->fp);
	}

	file->chapter_n_bytes += ret;
	file->total_n_bytes += ret;

	return ret;
}


int rmdPurgeCache(CacheData *cache_data_t, int sound)
{
	struct stat buff;
	char *fname;
	int exit_value = 0;
	int nth_cache = 1;

	fname = malloc(strlen(cache_data_t->imgdata) + 10);
	strcpy(fname, cache_data_t->imgdata);

	while (stat(fname, &buff) == 0) {
		if (remove(fname)) {
			fprintf(stderr, "Couldn't remove temporary file %s", cache_data_t->imgdata);
			exit_value = 1;
		}
		_rmdCacheFileN(cache_data_t->imgdata, &fname, nth_cache);
		nth_cache++;
	}

	free(fname);

	if (sound) {
		if (remove(cache_data_t->audiodata)) {
			fprintf(stderr, "Couldn't remove temporary file %s", cache_data_t->audiodata);
			exit_value = 1;
		}
	}

	if (remove(cache_data_t->specsfile)) {
		fprintf(stderr, "Couldn't remove temporary file %s", cache_data_t->specsfile);
		exit_value = 1;
	}

	if (remove(cache_data_t->projname)) {
		fprintf(stderr, "Couldn't remove temporary directory %s", cache_data_t->projname);
		exit_value = 1;
	}

	return exit_value;
}

void rmdInitCacheData(ProgData *pdata, EncData *enc_data_t, CacheData *cache_data_t)
{
	int	width, height, pid;
	char	pidbuf[8];

	//we set the buffer only since there's
	//no need to initialize the encoder from now.
	width = ((pdata->brwin.rrect.width + 15) >> 4) << 4;
	height = ((pdata->brwin.rrect.height + 15) >> 4) << 4;

	(pdata)->enc_data = enc_data_t;

	enc_data_t->yuv.y = (unsigned char *)malloc(height * width);
	enc_data_t->yuv.u = (unsigned char *)malloc(height * width / 4);
	enc_data_t->yuv.v = (unsigned char *)malloc(height * width / 4);
	enc_data_t->yuv.y_width = width;
	enc_data_t->yuv.y_height = height;
	enc_data_t->yuv.y_stride = width;

	enc_data_t->yuv.uv_width = width / 2;
	enc_data_t->yuv.uv_height = height / 2;
	enc_data_t->yuv.uv_stride = width / 2;

	//now we set the cache files
	(pdata)->cache_data = cache_data_t;

	cache_data_t->workdir = (pdata->args).workdir;
	pid = getpid();

	snprintf( pidbuf, 8, "%d", pid );
	//names are stored relatively to current dir(i.e. no chdir)
	cache_data_t->projname = malloc(strlen(cache_data_t->workdir) + 12 + strlen(pidbuf) + 3);
	//projname
	strcpy(cache_data_t->projname, cache_data_t->workdir);
	strcat(cache_data_t->projname, "/");
	strcat(cache_data_t->projname, "rMD-session-");
	strcat(cache_data_t->projname, pidbuf);
	strcat(cache_data_t->projname, "/");
	//image data
	cache_data_t->imgdata = malloc(strlen(cache_data_t->projname) + 11);
	strcpy(cache_data_t->imgdata, cache_data_t->projname);
	strcat(cache_data_t->imgdata, "img.out");
	//audio data
	cache_data_t->audiodata = malloc(strlen(cache_data_t->projname) + 10);
	strcpy(cache_data_t->audiodata, cache_data_t->projname);
	strcat(cache_data_t->audiodata, "audio.pcm");
	//specsfile
	cache_data_t->specsfile = malloc(strlen(cache_data_t->projname) + 10);
	strcpy(cache_data_t->specsfile, cache_data_t->projname);
	strcat(cache_data_t->specsfile, "specs.txt");

	//now that've got out buffers and our filenames we start
	//creating the needed files

	if (mkdir(cache_data_t->projname, 0777)) {
		fprintf(stderr, "Could not create temporary directory %s !!!\n", cache_data_t->projname);
		exit(13);
	}

	cache_data_t->icf = rmdCacheFileOpen(pdata, cache_data_t->imgdata, RMD_CACHE_FILE_MODE_WRITE);
	if (cache_data_t->icf == NULL) {
		fprintf(stderr, "Could not create temporary file %s !!!\n", cache_data_t->imgdata);
		exit(13);
	}

	if (!pdata->args.nosound) {
		cache_data_t->afp = fopen(cache_data_t->audiodata, "wb");
		if (cache_data_t->afp == NULL) {
			fprintf(stderr, "Could not create temporary file %s !!!\n", cache_data_t->audiodata);
			exit(13);
		}
	}

	if (rmdWriteSpecsFile(pdata)) {
		fprintf(stderr, "Could not write specsfile %s !!!\n", cache_data_t->specsfile);
		exit(13);
	}
}
