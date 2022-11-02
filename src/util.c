/*
 * util.c
 *
 * Utilities
 *
 * This file is part of zeroskip.
 *
 * zeroskip is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 *
 */

#include <libzeroskip/util.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>

#if defined(LINUX) || defined(DARWIN) || defined(BSD)
#include <fts.h>
#include <ftw.h>
#endif

#include <libgen.h>             /* For basename() */
#include <poll.h>
#include <string.h>
#include <sys/types.h>
#include <sys/param.h>          /* For PATH_MAX */
#include <sys/stat.h>
#include <unistd.h>

void *xmalloc(size_t size)
{
        void *ret;

        ret = malloc(size);
        if (!ret && !size)
                ret = malloc(1);

        if (!ret) {
                fprintf(stderr, "Out of memory. malloc failed.\n");
                exit(EXIT_FAILURE);
        }

        return ret;
}


void *xrealloc(void *ptr, size_t size)
{
        void *ret;

        ret = realloc(ptr, size);
        if (!ret && !size)
                ret = realloc(ptr, 1);

        if (!ret) {
                fprintf(stderr, "Out of memory. realloc failed.\n");
                exit(EXIT_FAILURE);
        }

        return ret;
}

void *xcalloc(size_t nmemb, size_t size)
{
        void *ret = NULL;

        if (!nmemb || !size)
                return ret;

        if (((size_t) - 1) / nmemb <= size) {
                fprintf(stderr, "Memory allocation error\n");
                exit(EXIT_FAILURE);
        }

        ret = (void *)calloc(nmemb, size);
        if (!ret) {
                fprintf(stderr, "Memory allocation error\n");
                exit(EXIT_FAILURE);
        }

        return ret;


}

char *xstrdup(const char *s)
{
        size_t len = strlen(s) + 1;
        char *ptr = xmalloc(len);

        memcpy(ptr, s, len);

        return ptr;
}

unsigned char *xucharbufdup(const unsigned char *s, size_t len)
{
        unsigned char *ptr = xmalloc(len);

        memcpy(ptr, s, len);

        return ptr;
}

/*
  xmkdir(): creates a directory if it doesn't exist.
  returns 0 on success, -1 otherwise. Check errno for
  the exact nature of the error.
 */
int xmkdir(const char *path, mode_t mode)
{
        int ret;
        struct stat sb;

        memset(&sb, 0, sizeof(struct stat));

        ret = stat(path, &sb);
        if (ret == -1) {        /* The directory doesn't exist, create */
                return mkdir(path, mode);
        }

        /* A directory/file exists in that path.
         */
        errno = EEXIST;

        return errno;
}

/*
  file_change_mode_rw():
  returns 0 if mode changed successfully, -1 otherwise.
 */
int file_change_mode_rw(const char *path)
{
        if (path && path[0])
                return chmod(path, S_IRUSR|S_IWUSR);

        return -1;
}

/*
  file_exists():
  returns TRUE if file exists, FALSE otherwise.
 */
bool_t file_exists(const char *file)
{
        if (file == NULL)
                return FALSE;

        if (access(file, F_OK) == 0)
                return  TRUE;

        return FALSE;
}


/*
  xrename():
  returns 0 if mode changed successfully, -1 otherwise.
 */
int xrename(const char *oldpath, const char *newpath)
{
        if ((oldpath  == NULL) || (newpath == NULL)) {
                return -1;
        }

        return rename(oldpath, newpath);
}

/*
  xunlink():
  returns 0 on success, non zero otherwise with errno set
  appropriately.
 */
int xunlink(const char *path)
{
        int ret;

        ret = unlink(path);
        if (!ret || errno == ENOENT)
                return 0;
        return ret;
}

#if defined(LINUX) || defined(DARWIN) || defined(BSD)
/* get_filenames_with_matching_prefix()
 * get files(only) in a given path, matching a prefix.
 * if `prefix` is NULL, get all files.
 * `list` contains a list of all paths
 * Returns 0 on success, non-zero otherwise with `errno` set
 * appropriately.
 */
int get_filenames_with_matching_prefix(char *const path[], const char *prefix,
                                       struct str_array *arr, int full_path)
{
        FTS *ftsp = NULL;
        FTSENT *fp = NULL;
        int fts_options = FTS_NOCHDIR;
        char *const def_path[] = {".", NULL};
        char buf[PATH_MAX];
        size_t prefixlen = 0;
        int err = 0;

        if (getcwd(buf, sizeof(buf)) == NULL)
                return errno;

        if (prefix && prefix[0])
                prefixlen = strlen(prefix);

        ftsp = fts_open(*path ? path : def_path, fts_options, NULL);
        if (ftsp == NULL) {
                perror("fts_open:");
                return errno;
        }

        while ((fp = fts_read(ftsp)) != NULL) {
                char *bname;
                char sbuf[MAX_BUF_PATH];
                int add = 0;

                if (fp->fts_info == FTS_DNR ||
                    fp->fts_info == FTS_ERR ||
                    fp->fts_info == FTS_NS) {
                        err = fp->fts_errno;
                        break;
                }

                if (fp->fts_info != FTS_F)
                        continue;

                bname = basename(fp->fts_path);

                if (full_path)
                        snprintf(sbuf, MAX_BUF_PATH, "%s/%s/%s", buf,
                                 *path ? *path : buf, bname);
                else
                        snprintf(sbuf, MAX_BUF_PATH, "%s/%s", *path ? *path : buf, bname);

                if (!prefix)
                        add = 1;
                else {
                        if (strncmp(bname, prefix, prefixlen) == 0)
                                add = 1;
                }


                if (add)
                        str_array_add(arr, sbuf);
        }

        fts_close(ftsp);

        if (err)
                errno = err;

        return err;
}

static int recursive_rm_cb(const char *path, const struct stat *sb _unused_,
                           int typeflag _unused_, struct FTW *ftwbuf _unused_)
{
        int ret = remove(path);

        if (ret)
                perror(path);

        return ret;
}

int recursive_rm(const char *path)
{
        return nftw(path, recursive_rm_cb, 64, FTW_DEPTH | FTW_PHYS);
}

#else
int get_filenames_with_matching_prefix(char *const path[]  _unused_,
                                       const char *prefix _unused_,
                                       struct str_array *arr _unused_,
                                       int full_path _unused_)
{
        return 0;
}

int recursive_rm(const char *path _unused_)
{
        return 0;               /* TODO: Not implemented yet. */
}

#endif  /* #if defined(LINUX) || defined(DARWIN) || defined(BSD) */

/**
 ** Time functions
 **/
/* Unix time in microseconds */
long long time_in_us(void)
{
        struct timeval tv;
        long long ust;

        gettimeofday(&tv, NULL);
        ust = ((long long)tv.tv_sec) * 1000000;
        ust += tv.tv_usec;

        return ust;
}

/* Unix time in milliseconds */
long long time_in_ms(void)
{
        return time_in_us()/1000;
}

/* sleep_ms():
 *  Sleeping in milliseconds
 */
void sleep_ms(uint32_t milliseconds)
{
        poll(NULL, 0, milliseconds);
}

/**
 ** File locking/unlocking functions.
 **/
enum LockAction {
        Unlock = 0,
        Lock = 1,
};

struct flockctx {
        dev_t st_dev;
        ino_t st_ino;
};

/*
 * The 'locker' function. Use the 'locker()' to lock or unlock a file.
 */
static int locker(int fd, enum LockAction action)
{
        struct flock fl;

        memset(&fl, 0, sizeof(fl));

        fl.l_type = (action ? F_WRLCK : F_UNLCK);
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;

        return fcntl(fd, F_SETLK, &fl);
}

/*
  file_lock():
  returns 0 if mode changed successfully, errno otherwise.
 */
int file_lock(int fd, struct flockctx **ctx)
{
        int err = 0;
        struct flockctx *data;
        struct stat sb;

        if (fd < 0) {
                err = errno;
                goto done;
        }

        if (fstat(fd, &sb) == -1) {
                err = errno;
                goto done;
        }

        data = xcalloc(1, sizeof(struct flockctx));
        data->st_dev = sb.st_dev;
        data->st_ino = sb.st_ino;

        /* TODO:Insert flockdata */
        /*
        if (insert_to_table(table, data) != 0) {
                xfree(data);
                err = -1;
                goto done;
        }
        */

        if (locker(fd, Lock) == -1) {
                err = errno;
                goto done;
        }

        *ctx =data;
done:
        return err;
}

/*
  file_unlock():
  returns 0 if mode changed successfully, -1 otherwise.
 */
int file_unlock(int fd, struct flockctx **ctx)
{
        int err = 0;
        struct flockctx *data = *ctx;

        if (fd < 0) {
                err = -1;
                goto done;
        }

        /* TODO: Remove flocdata */
        /*
        if (remove_from_table(table, data) != 0) {
                err = -1;
                goto done;
        }
        */

        if (locker(fd, Unlock) == -1) {
                err = errno;
                goto done;
        }

        xfree(data);
        *ctx = NULL;
done:
        return err;
}


int natural_strcasecmp(const char *s1, const char *s2)
{
        const char *a = s1;
        const char *b = s2;

        if (!a || !b) {
                return a ? 1 : b ? -1 : 0;
        }

        if (isdigit(*a) && isdigit(*b)) {
                char *remA;
                char *remB;
                long valA = strtol(a, &remA, 10);
                long valB = strtol(b, &remB, 10);
                if (valA != valB) {
                        return valA - valB;
                } else if ((remB - b) != (remA - a)) {  /* equal but varying lengths */
                        return (remB - b) - (remA - a);
                } else {
                        return natural_strcasecmp(remA, remB);
                }
        }

        if (isdigit(*a) || isdigit(*b)) {    /* Just one of the strings is a number */
                return isdigit(*a) ? -1 : 1;
        }

        while (*a && *b) {  /* non-numeric characters */
                if (isdigit(*a) || isdigit(*b))
                        return natural_strcasecmp(a, b);

                if (tolower(*a) != tolower(*b))
                        return tolower(*a) - tolower(*b);

                a++;
                b++;
        }

        return *a ? 1 : *b ? -1 : 0;
}
