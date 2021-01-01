/*
 * Copyright 2003 Phil Mellor <monkeyson@users.sourceforge.net>
 * Copyright 2007 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2004 Andrew Timmins <atimmins@blueyonder.co.uk>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <locale.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <libwapcaplet/libwapcaplet.h>

#include "netsurf/inttypes.h"
#include "utils/config.h"
#include "utils/nsoption.h"
#include "utils/corestrings.h"
#include "utils/log.h"
#include "utils/string.h"
#include "utils/utf8.h"
#include "utils/messages.h"
#include "content/content_factory.h"
#include "content/fetchers.h"
#include "content/hlcache.h"
#include "content/mimesniff.h"
#include "content/urldb.h"
#include "css/css.h"
#include "image/image.h"
#include "image/image_cache.h"
#include "javascript/js.h"
#include "html/html.h"
#include "text/textplain.h"

#include "netsurf/browser_window.h"
#include "desktop/system_colour.h"
#include "desktop/searchweb.h"
#include "netsurf/misc.h"
#include "desktop/gui_internal.h"
#include "netsurf/netsurf.h"

/** speculative pre-conversion small image size
 *
 * Experimenting by visiting every page from default page in order and
 * then netsurf homepage
 *
 * 0    : Cache hit/miss/speculative miss/fail 604/147/  0/0 (80%/19%/ 0%/ 0%)
 * 2048 : Cache hit/miss/speculative miss/fail 622/119/ 17/0 (82%/15%/ 2%/ 0%)
 * 4096 : Cache hit/miss/speculative miss/fail 656/109/ 25/0 (83%/13%/ 3%/ 0%)
 * 8192 : Cache hit/miss/speculative miss/fail 648/104/ 40/0 (81%/13%/ 5%/ 0%)
 * ALL  : Cache hit/miss/speculative miss/fail 775/  0/161/0 (82%/ 0%/17%/ 0%)
*/
#define SPECULATE_SMALL 4096

/** the time between image cache clean runs in ms. */
#define IMAGE_CACHE_CLEAN_TIME (10 * 1000)

/** default time between content cache cleans. */
#define HL_CACHE_CLEAN_TIME (2 * IMAGE_CACHE_CLEAN_TIME)

/** ensure there is a minimal amount of memory for source objetcs and
 * decoded bitmaps.
 */
#define MINIMUM_MEMORY_CACHE_SIZE (2 * 1024 * 1024)

/** default minimum object time before object is pushed to backing store. (s) */
#define LLCACHE_STORE_MIN_LIFETIME (60 * 30)

/** default minimum bandwidth for backing store writeout. (byte/s) */
#define LLCACHE_STORE_MIN_BANDWIDTH (128 * 1024)

/** default maximum bandwidth for backing store writeout. (byte/s) */
#define LLCACHE_STORE_MAX_BANDWIDTH (1024 * 1024)

/** default time quantum with which to calculate bandwidth (ms) */
#define LLCACHE_STORE_TIME_QUANTUM (100)

static void netsurf_lwc_iterator(lwc_string *str, void *pw)
{
	NSLOG(netsurf, WARNING, "[%3u] %.*s", str->refcnt,
	      (int)lwc_string_length(str), lwc_string_data(str));
}

/**
 * Build a "username:password" from components.
 *
 * \param[in]  username      The username component.
 * \param[in]  password      The password component.
 * \param[out] userpass_out  Returns combined string on success.
 *                           Owned by caller.
 * \return NSERROR_OK, or appropriate error code.
 */
static nserror netsurf__build_userpass(
		const char *username,
		const char *password,
		char **userpass_out)
{
	char *userpass;
	size_t len;

	len = strlen(username) + 1 + strlen(password) + 1;

	userpass = malloc(len);
	if (userpass == NULL) {
		return NSERROR_NOMEM;
	}

	snprintf(userpass, len, "%s:%s", username, password);

	*userpass_out = userpass;
	return NSERROR_OK;
}

/**
 * Unpack a "username:password" to components.
 *
 * \param[in]  userpass      The input string to split.
 * \param[in]  username_out  Returns username on success.  Owned by caller.
 * \param[out] password_out  Returns password on success.  Owned by caller.
 * \return NSERROR_OK, or appropriate error code.
 */
static nserror netsurf__unpack_userpass(
		const char *userpass,
		char **username_out,
		char **password_out)
{
	const char *tmp;
	char *username;
	char *password;
	size_t len;

	if (userpass == NULL) {
		username = malloc(1);
		password = malloc(1);
		if (username == NULL || password == NULL) {
			free(username);
			free(password);
			return NSERROR_NOMEM;
		}
		username[0] = '\0';
		password[0] = '\0';

		*username_out = username;
		*password_out = password;
		return NSERROR_OK;
	}

	tmp = strchr(userpass, ':');
	if (tmp == NULL) {
		return NSERROR_BAD_PARAMETER;
	} else {
		size_t len2;
		len = tmp - userpass;
		len2 = strlen(++tmp);

		username = malloc(len + 1);
		password = malloc(len2 + 1);
		if (username == NULL || password == NULL) {
			free(username);
			free(password);
			return NSERROR_NOMEM;
		}
		memcpy(username, userpass, len);
		username[len] = '\0';
		memcpy(password, tmp, len2 + 1);
	}

	*username_out = username;
	*password_out = password;
	return NSERROR_OK;
}

/**
 * Contect for login callbacks to front ends.
 */
struct auth_data {
	char *realm;
	nsurl *url;

	llcache_query_response cb;
	void *pw;
};

/**
 * Callback function passed to front ends for handling logins.
 *
 * \param[in]  username  The username.
 * \param[in]  password  The password.
 * \param[in]  cbpw      Our context.
 * \return NSERROR_OK, or appropriate error code.
 */
static nserror netsurf__handle_login_response(
		const char *username,
		const char *password,
		void *cbpw)
{
	struct auth_data *ctx = cbpw;
	bool proceed = false;
	nserror err;

	if (username != NULL && password != NULL) {
		char *userpass;

		err = netsurf__build_userpass(username, password, &userpass);
		if (err != NSERROR_OK) {
			return err;
		}

		urldb_set_auth_details(ctx->url, ctx->realm, userpass);
		free(userpass);
		proceed = true;
	}

	err = ctx->cb(proceed, ctx->pw);
	nsurl_unref(ctx->url);
	free(ctx->realm);
	free(ctx);
	return err;
}

/**
 * Helper for getting front end to handle logins.
 *
 * \param[in] query  Query descriptor
 * \param[in] pw     Private data
 * \param[in] cb     Continuation callback
 * \param[in] cbpw   Private data for continuation
 * \return NSERROR_OK, or appropriate error code.
 */
static nserror netsurf__handle_login(const llcache_query *query,
		void *pw, llcache_query_response cb, void *cbpw)
{
	struct auth_data *ctx;
	char *username;
	char *password;
	nserror err;

	NSLOG(llcache, INFO, "HTTP Auth for: %s: %s",
			query->data.auth.realm, nsurl_access(query->url));

	ctx = malloc(sizeof(*ctx));
	if (ctx == NULL) {
		return NSERROR_NOMEM;
	}

	ctx->realm = strdup(query->data.auth.realm);
	if (ctx->realm == NULL) {
		free(ctx);
		return NSERROR_NOMEM;
	}
	ctx->url = nsurl_ref(query->url);
	ctx->cb = cb;
	ctx->pw = cbpw;

	err = netsurf__unpack_userpass(
			urldb_get_auth_details(ctx->url, ctx->realm),
			&username, &password);
	if (err != NSERROR_OK) {
		nsurl_unref(ctx->url);
		free(ctx->realm);
		free(ctx);
		return err;
	}

	err = guit->misc->login(ctx->url, ctx->realm, username, password,
			netsurf__handle_login_response, ctx);
	free(username);
	free(password);
	if (err != NSERROR_OK) {
		ctx->cb(false, ctx->pw);
		nsurl_unref(ctx->url);
		free(ctx->realm);
		free(ctx);
		return err;
	}

	return NSERROR_OK;
}

/**
 * Dispatch a low-level cache query to the frontend
 *
 * \param query  Query descriptor
 * \param pw     Private data
 * \param cb     Continuation callback
 * \param cbpw   Private data for continuation
 * \return NSERROR_OK
 */
static nserror netsurf_llcache_query_handler(const llcache_query *query,
		void *pw, llcache_query_response cb, void *cbpw)
{
	nserror res = NSERROR_OK;

	switch (query->type) {
	case LLCACHE_QUERY_AUTH:
		res = netsurf__handle_login(query, pw, cb, cbpw);
		break;

	case LLCACHE_QUERY_REDIRECT:
		/** \todo Need redirect query dialog */
		/* For now, do nothing, as this query type isn't emitted yet */
		break;

	case LLCACHE_QUERY_SSL:
		res = guit->misc->cert_verify(query->url, query->data.ssl.certs,
				query->data.ssl.num, cb, cbpw);
		break;
	}

	return res;
}

/* exported interface documented in netsurf/netsurf.h */
nserror netsurf_init(const char *store_path)
{
	nserror ret;
	struct hlcache_parameters hlcache_parameters = {
		.bg_clean_time = HL_CACHE_CLEAN_TIME,
		.llcache = {
			.cb = netsurf_llcache_query_handler,
			.minimum_lifetime = LLCACHE_STORE_MIN_LIFETIME,
			.minimum_bandwidth = LLCACHE_STORE_MIN_BANDWIDTH,
			.maximum_bandwidth = LLCACHE_STORE_MAX_BANDWIDTH,
			.time_quantum = LLCACHE_STORE_TIME_QUANTUM,
		}
	}; 
	struct image_cache_parameters image_cache_parameters = {
		.bg_clean_time = IMAGE_CACHE_CLEAN_TIME,
		.speculative_small = SPECULATE_SMALL
	};
	
#ifdef HAVE_SIGPIPE
	/* Ignore SIGPIPE - this is necessary as OpenSSL can generate these
	 * and the default action is to terminate the app. There's no easy
	 * way of determining the cause of the SIGPIPE (other than using
	 * sigaction() and some mechanism for getting the file descriptor
	 * out of libcurl). However, we expect nothing else to generate a
	 * SIGPIPE, anyway, so may as well just ignore them all.
	 */
	signal(SIGPIPE, SIG_IGN);
#endif

	/* corestrings init */
	ret = corestrings_init();
	if (ret != NSERROR_OK)
		return ret;

	/* set up cache limits based on the memory cache size option */
	hlcache_parameters.llcache.limit = nsoption_int(memory_cache_size);

	if (hlcache_parameters.llcache.limit < MINIMUM_MEMORY_CACHE_SIZE) {
		hlcache_parameters.llcache.limit = MINIMUM_MEMORY_CACHE_SIZE;
		NSLOG(netsurf, INFO,
		      "Setting minimum memory cache size %"PRIsizet,
		      hlcache_parameters.llcache.limit);
	} 

	/* Set up the max attempts made to fetch a timing out resource */
	hlcache_parameters.llcache.fetch_attempts = nsoption_uint(max_retried_fetches);

	/* image cache is 25% of total memory cache size */
	image_cache_parameters.limit = (hlcache_parameters.llcache.limit * 25) / 100;

	/* image cache hysteresis is 20% of the image cache size */
	image_cache_parameters.hysteresis = (image_cache_parameters.limit * 20) / 100;

	/* account for image cache use from total */
	hlcache_parameters.llcache.limit -= image_cache_parameters.limit;

	/* set backing store target limit */
	hlcache_parameters.llcache.store.limit = nsoption_uint(disc_cache_size);

	/* set backing store hysterissi to 20% */
	hlcache_parameters.llcache.store.hysteresis = (hlcache_parameters.llcache.store.limit * 20) / 100;;

	/* set the path to the backing store */
	hlcache_parameters.llcache.store.path = store_path;

	/* image handler bitmap cache */
	ret = image_cache_init(&image_cache_parameters);
	if (ret != NSERROR_OK)
		return ret;

	/* content handler initialisation */
	ret = nscss_init();
	if (ret != NSERROR_OK)
		return ret;

	ret = html_init();
	if (ret != NSERROR_OK)
		return ret;

	ret = image_init();
	if (ret != NSERROR_OK)
		return ret;

	ret = textplain_init();
	if (ret != NSERROR_OK)
		return ret;

	setlocale(LC_ALL, "");

	/* initialise the fetchers */
	ret = fetcher_init();
	if (ret != NSERROR_OK)
		return ret;
	
	/* Initialise the hlcache and allow it to init the llcache for us */
	ret = hlcache_initialise(&hlcache_parameters);
	if (ret != NSERROR_OK)
		return ret;

	/* Initialize system colours */
	ret = ns_system_colour_init();
	if (ret != NSERROR_OK)
		return ret;

	js_initialise();

	return NSERROR_OK;
}


/**
 * Clean up components used by gui NetSurf.
 */

void netsurf_exit(void)
{
	hlcache_stop();
	
	NSLOG(netsurf, INFO, "Closing GUI");
	guit->misc->quit();
	
	NSLOG(netsurf, INFO, "Finalising JavaScript");
	js_finalise();

	NSLOG(netsurf, INFO, "Finalising Web Search");
	search_web_finalise();

	NSLOG(netsurf, INFO, "Finalising high-level cache");
	hlcache_finalise();

	NSLOG(netsurf, INFO, "Closing fetches");
	fetcher_quit();

	/* dump any remaining cache entries */
	image_cache_fini();

	/* Clean up after content handlers */
	content_factory_fini();

	NSLOG(netsurf, INFO, "Closing utf8");
	utf8_finalise();

	NSLOG(netsurf, INFO, "Destroying URLdb");
	urldb_destroy();

	NSLOG(netsurf, INFO, "Destroying System colours");
	ns_system_colour_finalize();

	NSLOG(netsurf, INFO, "Destroying Messages");
	messages_destroy();

	corestrings_fini();
	NSLOG(netsurf, INFO, "Remaining lwc strings:");
	lwc_iterate_strings(netsurf_lwc_iterator, NULL);

	NSLOG(netsurf, INFO, "Exited successfully");
}