/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <dlfcn.h>
#include <argz.h>
#include <dirent.h>
#include <sys/stat.h>
#include <jansson.h>

#include "module.h"
#include "message.h"
#include "rpc.h"

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"

struct flux_modlist_struct {
    json_t *o;
};

/* Get service name from module name string.
 */
static char *mod_service (const char *modname)
{
    char *service = NULL;
    if (strchr (modname, '.')) {
        service = xstrdup (modname);
        char *p = strrchr (service, '.');
        *p = '\0';
    } else
        service = xstrdup ("cmb");
    return service;
}

/**
 ** JSON encode/decode functions
 **/

int flux_insmod_json_decode (const char *json_str,
                             char **pathp, char **argz, size_t *argz_len)
{
    json_error_t error;
    json_t *o = NULL;
    json_t *args, *value;
    const char *path, *arg;
    size_t index;
    int e, rc = -1;

    if (!(o = json_loads (json_str, 0, &error))
            || json_unpack (o, "{s:s, s:o}", "path", &path, "args", &args) < 0
            || !json_is_array (args)) {
        errno = EPROTO;
        goto done;
    }
    json_array_foreach (args, index, value) {
        if (!(arg = json_string_value (value))) {
            errno = EPROTO;
            goto done;
        }
        if ((e = argz_add (argz, argz_len, arg)) != 0) {
            errno = e;
            goto done;
        }
    }
    if (pathp)
        *pathp = xstrdup (path);
    rc = 0;
done:
    json_decref (o);
    return rc;
}

char *flux_insmod_json_encode (const char *path, int argc, char **argv)
{
    json_t *args, *o = NULL;
    char *json_str = NULL;
    int i;

    if (!(args = json_array())) {
        errno = ENOMEM;
        goto done;
    }
    for (i = 0; i < argc; i++) {
        json_t *value = json_string (argv[i]);
        if (!value || json_array_append_new (args, value) < 0) {
            json_decref (args);
            errno = ENOMEM;
            goto done;
        }
    }
    if (!(o = json_pack ("{s:s, s:o}", "path", path, "args", args))) {
        errno = ENOMEM;
        goto done;
    }
    json_str = json_dumps (o, JSON_COMPACT);
done:
    json_decref (o);
    return json_str;
}

int flux_rmmod_json_decode (const char *json_str, char **name)
{
    json_error_t error;
    json_t *o = NULL;
    const char *s;
    int rc = -1;

    if (!(o = json_loads (json_str, 0, &error))
            || json_unpack (o, "{s:s}", "name", &s) < 0) {
        errno = EPROTO;
        goto done;
    }
    if (name)
        *name = xstrdup (s);
    rc = 0;
done:
    json_decref (o);
    return rc;
}

char *flux_rmmod_json_encode (const char *name)
{
    json_t *o;
    char *json_str = NULL;

    if (!(o = json_pack ("{s:s}", "name", name))) {
        errno = ENOMEM;
        goto done;
    }
    json_str = json_dumps (o, JSON_COMPACT);
done:
    json_decref (o);
    return json_str;
}

int flux_modlist_get (flux_modlist_t *mods, int n, const char **name, int *size,
                      const char **digest, int *idle, int *status)
{
    json_t *a, *o;
    int rc = -1;

    if (!(a = json_object_get (mods->o, "mods")) || !json_is_array (a)
                                        || !(o = json_array_get (a, n))) {
        errno = EPROTO;
        goto done;
    }
    if (json_unpack (o, "{s:s, s:i, s:s, s:i, s:i}", "name", name,
                    "size", size, "digest", digest, "idle", idle,
                    "status", status) < 0) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int flux_modlist_count (flux_modlist_t *mods)
{
    json_t *a;

    if (!(a = json_object_get (mods->o, "mods")) || !json_is_array (a)) {
        errno = EPROTO;
        return -1;
    }
    return json_array_size (a);
}

int flux_modlist_append (flux_modlist_t *mods, const char *name, int size,
                            const char *digest, int idle, int status)
{
    json_t *a, *o;
    int rc = -1;

    if (!(a = json_object_get (mods->o, "mods")) || !json_is_array (a)) {
        errno = EPROTO;
        goto done;
    }
    if (!(o = json_pack ("{s:s, s:i, s:s, s:i, s:i}", "name", name,
                         "size", size, "digest", digest, "idle", idle,
                          "status", status))) {
        errno = ENOMEM;
        goto done;
    }
    if (json_array_append_new (a, o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

void flux_modlist_destroy (flux_modlist_t *mods)
{
    if (mods) {
        json_decref (mods->o);
        free (mods);
    }
}

flux_modlist_t *flux_modlist_create (void)
{
    flux_modlist_t *mods = calloc (1, sizeof (*mods));
    if (!mods || !(mods->o = json_array ())
              || !(mods->o = json_pack ("{s:o}", "mods", mods->o))) {
        flux_modlist_destroy (mods);
        errno = ENOMEM;
        return NULL;
    }
    return mods;
}

char *flux_lsmod_json_encode (flux_modlist_t *mods)
{
    return json_dumps (mods->o, JSON_COMPACT);
}

flux_modlist_t *flux_lsmod_json_decode (const char *json_str)
{
    flux_modlist_t *mods = xzmalloc (sizeof (*mods));
    json_error_t error;
    if (!(mods->o = json_loads (json_str, 0, &error))) {
        free (mods);
        errno = EPROTO;
        return NULL;
    }
    return mods;
}

char *flux_modname(const char *path)
{
    void *dso;
    const char **np;
    char *name = NULL;

    dlerror ();
    if ((dso = dlopen (path, RTLD_LAZY | RTLD_LOCAL | RTLD_DEEPBIND))) {
        if ((np = dlsym (dso, "mod_name")) && *np)
            name = xstrdup (*np);
        dlclose (dso);
        return name;
    }
    // Another reporting method may be warranted here, but when a dynamic
    // library dependency doesn't resolve, it really helps to know that's
    // the error.  Otherwise it prints as "invalid argument" from the
    // broker.
    log_msg ("%s", dlerror ());
    errno = ENOENT;
    return NULL;
}

/* helper for flux_modfind() */
static int flux_modname_cmp(const char *path, const char *name)
{
    char * modname = flux_modname(path);
    int rc = modname ? strcmp(modname, name) : -1;
    free(modname);
    return rc;
}

#ifndef MAX
#define MAX(a,b)    ((a)>(b)?(a):(b))
#endif

/* helper for flux_modfind() */
static int strcmpend (const char *s1, const char *s2)
{
    int skip = MAX (strlen (s1) - strlen (s2), 0);
    return strcmp (s1 + skip, s2);
}

/* helper for flux_modfind() */
static char *modfind (const char *dirpath, const char *modname)
{
    DIR *dir;
    struct dirent entry, *dent;
    char *modpath = NULL;
    struct stat sb;
    char path[PATH_MAX];
    size_t len = sizeof (path);

    if (!(dir = opendir (dirpath)))
        goto done;
    while (!modpath) {
        if ((errno = readdir_r (dir, &entry, &dent)) > 0 || dent == NULL)
            break;
        if (!strcmp (dent->d_name, ".") || !strcmp (dent->d_name, ".."))
            continue;
        if (snprintf (path, len, "%s/%s", dirpath, dent->d_name) >= len) {
            errno = EINVAL;
            break;
        }
        if (stat (path, &sb) == 0) {
            if (S_ISDIR (sb.st_mode))
                modpath = modfind (path, modname);
            else if (!strcmpend (path, ".so")) {
                if (!flux_modname_cmp (path, modname))
                    if (!(modpath = realpath (path, NULL)))
                        oom ();

            }
        }
    }
    closedir (dir);
done:
    return modpath;
}

char *flux_modfind (const char *searchpath, const char *modname)
{
    char *cpy = xstrdup (searchpath);
    char *dirpath, *saveptr = NULL, *a1 = cpy;
    char *modpath = NULL;

    while ((dirpath = strtok_r (a1, ":", &saveptr))) {
        if ((modpath = modfind (dirpath, modname)))
            break;
        a1 = NULL;
    }
    free (cpy);
    if (!modpath)
        errno = ENOENT;
    return modpath;
}

int flux_rmmod (flux_t *h, uint32_t nodeid, const char *name)
{
    flux_rpc_t *r = NULL;
    char *service = mod_service (name);
    char *topic = xasprintf ("%s.rmmod", service);
    char *json_str = NULL;
    int rc = -1;

    if (!(json_str = flux_rmmod_json_encode (name)))
        goto done;
    if (!(r = flux_rpc (h, topic, json_str, nodeid, 0)))
        goto done;
    if (flux_rpc_get (r, NULL) < 0)
        goto done;
    rc = 0;
done:
    free (service);
    free (topic);
    if (json_str)
        free (json_str);
    if (r)
        flux_rpc_destroy (r);
    return rc;
}

int flux_lsmod (flux_t *h, uint32_t nodeid, const char *service,
                flux_lsmod_f cb, void *arg)
{
    flux_rpc_t *r = NULL;
    char *topic = xasprintf ("%s.lsmod", service ? service : "cmb");
    flux_modlist_t *mods = NULL;
    const char *json_str;
    int rc = -1;
    int i, len;

    if (!(r = flux_rpc (h, topic, NULL, nodeid, 0)))
        goto done;
    if (flux_rpc_get (r, &json_str) < 0)
        goto done;
    if (!(mods = flux_lsmod_json_decode (json_str)))
        goto done;
    if ((len = flux_modlist_count (mods)) == -1)
        goto done;
    for (i = 0; i < len; i++) {
        const char *name, *digest;
        int size, idle, status;
        if (flux_modlist_get (mods, i, &name, &size, &digest, &idle,
                                                              &status) < 0)
            goto done;
        if (cb (name, size, digest, idle, status, NULL, arg) < 0)
            goto done;
    }
    rc = 0;
done:
    free (topic);
    if (mods)
        flux_modlist_destroy (mods);
    if (r)
        flux_rpc_destroy (r);
    return rc;
}

int flux_insmod (flux_t *h, uint32_t nodeid, const char *path,
                 int argc, char **argv)
{
    flux_rpc_t *r = NULL;
    char *name = NULL;
    char *service = NULL;
    char *topic = NULL;
    char *json_str = NULL;
    int rc = -1;

    if (!(name = flux_modname (path))) {
        goto done;
    }
    service = mod_service (name);
    topic = xasprintf ("%s.insmod", service);

    json_str = flux_insmod_json_encode (path, argc, argv);
    if (!(r = flux_rpc (h, topic, json_str, nodeid, 0)))
        goto done;
    if (flux_rpc_get (r, NULL) < 0)
        goto done;
    rc = 0;
done:
    if (name)
        free (name);
    if (service)
        free (service);
    if (topic)
        free (topic);
    if (json_str)
        free (json_str);
    if (r)
        flux_rpc_destroy (r);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
