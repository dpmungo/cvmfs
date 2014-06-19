/**
 * This file is part of the CernVM File System.
 *
 * libcvmfs provides an API for the CernVM-FS client.  This is an
 * alternative to FUSE for reading a remote CernVM-FS repository.
 */

#define _FILE_OFFSET_BITS 64

#include "cvmfs_config.h"

#include <sys/stat.h>
#include <errno.h>
#include <cstdlib>
#include <stddef.h>

#include <string>

#include "util.h"
#include "logging.h"
#include "smalloc.h"
#include "libcvmfs_int.h"
#include "libcvmfs.h"

using namespace std;  // NOLINT


int set_option(char const *name, char const *value, bool *var) {
  if( *value != '\0' ) {
    fprintf(stderr,"Option %s=%s contains a value when none was expected.\n",name,value);
    return -1;
  }
  *var = true;
  return 0;
}

int set_option(char const *name, char const *value, unsigned *var) {
  unsigned v = 0;
  int end = 0;
  int rc = sscanf(value,"%u%n",&v,&end);
  if( rc != 1 || value[end] != '\0' ) {
    fprintf(stderr,"Invalid unsigned integer value for %s=%s\n",name,value);
    return -1;
  }
  *var = v;
  return 0;
}

int set_option(char const *name, char const *value, unsigned long *var) {
  unsigned long v = 0;
  int end = 0;
  int rc = sscanf(value,"%lu%n",&v,&end);
  if( rc != 1 || value[end] != '\0' ) {
    fprintf(stderr,"Invalid unsigned long integer value for %s=%s\n",name,value);
    return -1;
  }
  *var = v;
  return 0;
}

int set_option(char const *name, char const *value, int *var) {
  int v = 0;
  int end = 0;
  int rc = sscanf(value,"%d%n",&v,&end);
  if( rc != 1 || value[end] != '\0' ) {
    fprintf(stderr,"Invalid integer value for %s=%s\n",name,value);
    return -1;
  }
  *var = v;
  return 0;
}

int set_option(char const *name, char const *value, string *var) {
  *var = value;
  return 0;
}


#define CVMFS_OPT(var) if( strcmp(name,#var)==0 ) return ::set_option(name,value,&var)

struct cvmfs_repo_options : public cvmfs_context::options {
  int set_option(char const *name, char const *value) {
    CVMFS_OPT(url);
    CVMFS_OPT(timeout);
    CVMFS_OPT(timeout_direct);
    CVMFS_OPT(proxies);
    CVMFS_OPT(tracefile);
    CVMFS_OPT(allow_unsigned);
    CVMFS_OPT(pubkey);
    CVMFS_OPT(deep_mount);
    CVMFS_OPT(repo_name);
    CVMFS_OPT(mountpoint);
    CVMFS_OPT(blacklist);

    fprintf(stderr,"Unknown repo option: %s\n", name);
    return -1;
  }

  int verify_sanity() {
    if( mountpoint.empty() && !repo_name.empty()) {
      mountpoint = "/cvmfs/";
      mountpoint += repo_name;
    }
    while( mountpoint.length()>0 && mountpoint[mountpoint.length()-1] == '/' ) {
      mountpoint.resize(mountpoint.length()-1);
    }

    return 0;
  }
};

struct cvmfs_global_options : public cvmfs_globals::options {
  int set_option(char const *name, char const *value) {
    CVMFS_OPT(cache_directory);
    CVMFS_OPT(change_to_cache_directory);
    CVMFS_OPT(log_syslog_level);
    CVMFS_OPT(log_prefix);
    CVMFS_OPT(log_file);
    CVMFS_OPT(max_open_files);

    fprintf(stderr,"Unknown global option: %s\n", name);
    return -1;
  }

  int verify_sanity() {
    return 0;
  }
};


/**
 * Structure to parse the file system options.
 */
template <class DerivedT>
struct cvmfs_options : public DerivedT {

  //#define CVMFS_OPT(var) if( strcmp(name,#var)==0 ) return cvmfs_options::set_option(name,value,&var)
  int set_option(char const *name, char const *value) {
    return DerivedT::set_option(name, value);
  }

  int parse_options(char const *options)
  {
    while( *options ) {
      char const *next = options;
      string name;
      string value;

      // get the option name
      for( next=options; *next && *next != ',' && *next != '='; next++ ) {
        if( *next == '\\' ) {
          next++;
          if( *next == '\0' ) break;
        }
        name += *next;
      }

      if( *next == '=' ) {
        next++;
      }

      // get the option value
      for(; *next && *next != ','; next++ ) {
        if( *next == '\\' ) {
          next++;
          if( *next == '\0' ) break;
        }
        value += *next;
      }

      if( !name.empty() || !value.empty() ) {
        int result = set_option(name.c_str(), value.c_str());
        if (result != 0) {
          return result;
        }
      }

      if( *next == ',' ) next++;
      options = next;
    }

    return DerivedT::verify_sanity();
  }
};

typedef cvmfs_options<cvmfs_repo_options>   repo_options;
typedef cvmfs_options<cvmfs_global_options> global_options;

  /**
   * Display the usage message.
   */
  static void usage() {
    struct cvmfs_repo_options defaults;
    fprintf(stderr,
            "CernVM-FS version %s\n"
            "Copyright (c) 2009- CERN\n"
            "All rights reserved\n\n"
            "Please visit http://cernvm.cern.ch/project/info for license details and author list.\n\n"

            "libcvmfs options are expected in the form: option1,option2,option3,...\n"
            "Within an option, the characters , and \\ must be preceded by \\.\n\n"

            "options are:\n"
            " url=REPOSITORY_URL      The URL of the CernVM-FS server(s): 'url1;url2;...'\n"
            " timeout=SECONDS         Timeout for network operations (default is %d)\n"
            " timeout_direct=SECONDS  Timeout for network operations without proxy (default is %d)\n"
            " cachedir=DIR            Where to store disk cache\n"
            " alien_cachedir=DIR      Store data chunks separately\n"
            " proxies=HTTP_PROXIES    Set the HTTP proxy list, such as 'proxy1|proxy2;DIRECT'\n"
            " tracefile=FILE          Trace FUSE opaerations into FILE\n"
            " pubkey=PEMFILE          Public RSA key that is used to verify the whitelist signature.\n"
            " allow_unsigned          Accept unsigned catalogs (allows man-in-the-middle attacks)\n"
            " rebuild_cachedb         Force rebuilding the quota cache db from cache directory\n"
            " quota_limit=MB          Limit size of data chunks in cache. -1 Means unlimited.\n"
            " quota_threshold=MB      Cleanup until size is <= threshold\n"
            " nofiles=NUMBER          Set the maximum number of open files for CernVM-FS process (soft limit)\n"
            " logfile=FILE            Logs all messages to FILE instead of stderr and daemonizes.\n"
            "                         Makes only sense for the debug version\n"
            " deep_mount=prefix       Path prefix if a repository is mounted on a nested catalog,\n"
            "                         i.e. deep_mount=/software/15.0.1\n"
            " repo_name=<repository>  Unique name of the mounted repository, e.g. atlas.cern.ch\n"
            " mountpoint=<path>       Path to root of repository, e.g. /cvmfs/atlas.cern.ch\n"
            " blacklist=FILE          Local blacklist for invalid certificates.  Has precedence over the whitelist.\n"
            " syslog_level=NUMBER     Sets the level used for syslog to DEBUG (1), INFO (2), or NOTICE (3).\n"
            "                         Default is NOTICE.\n"
            " Note: you cannot load files greater than quota_limit-quota_threshold\n",
            PACKAGE_VERSION, defaults.timeout, defaults.timeout_direct
            );
  }

/* Path to root of repository.  Used to resolve absolute symlinks. */
static string mountpoint;

/* Expand symlinks in all levels of a path.  Also, expand ".." and
 * ".".  This also has the side-effect of ensuring that
 * cvmfs_getattr() is called on all parent paths, which is needed to
 * ensure proper loading of nested catalogs before the child is
 * accessed.
 */
static int expand_path(cvmfs_context *ctx,
                       char const    *path,
                       string        &expanded_path,
                       int            depth = 0)
{
  string p_path = GetParentPath(path);
  string fname = GetFileName(path);
  int rc;

  if( fname == ".." ) {
    rc = expand_path(ctx, p_path.c_str(), expanded_path, depth);
    if( rc != 0 ) {
      return -1;
    }
    if( expanded_path == "/" ) {
      // attempt to access parent path of the root of the repository
      LogCvmfs(kLogCvmfs,kLogDebug,"libcvmfs cannot resolve symlinks to paths outside of the repository: %s",path);
      errno = ENOENT;
      return -1;
    }
    expanded_path = GetParentPath(expanded_path);
    if( expanded_path == "" ) {
      expanded_path = "/";
    }
    return 0;
  }

  string buf;
  if( p_path != "" ) {
    rc = expand_path(ctx, p_path.c_str(), buf, depth);
    if( rc != 0 ) {
      return -1;
    }

    if( fname == "." ) {
      expanded_path = buf;
      return 0;
    }
  }

  if( buf.length() == 0 || buf[buf.length()-1] != '/' ) {
	  buf += "/";
  }
  buf += fname;

  struct stat st;
  rc = ctx->GetAttr(buf.c_str(),&st);
  if( rc != 0 ) {
    errno = -rc;
    return -1;
  }

  if( !S_ISLNK(st.st_mode) ) {
    expanded_path = buf;
    return 0;
  }

  if( depth > 1000 ) {
    // avoid unbounded recursion due to symlinks
    LogCvmfs(kLogCvmfs,kLogDebug,"libcvmfs hit its symlink recursion limit: %s",path);
    errno = ELOOP;
    return -1;
  }

  // expand symbolic link

  char *ln_buf = (char *)smalloc(st.st_size+2);
  if( !ln_buf ) {
    errno = ENOMEM;
    return -1;
  }
  rc = ctx->Readlink(buf.c_str(), ln_buf, st.st_size + 2);
  if( rc != 0 ) {
    free(ln_buf);
    errno = -rc;
    return -1;
  }
  if( ln_buf[0] == '/' ) {
    // symlink is absolute path
    // convert /cvmfs/repo/blah --> /blah
	  size_t len = ::mountpoint.length();
    if( strncmp(ln_buf,::mountpoint.c_str(),len)==0
       && (ln_buf[len] == '/' || ln_buf[len] == '\0'))
    {
      buf = ln_buf+len;
	  if( ln_buf[len] == '\0' ) {
		  buf += "/";
	  }
    }
    else {
      LogCvmfs(kLogCvmfs,kLogDebug,"libcvmfs cannot resolve symlinks to paths outside of the repository: %s --> %s (mountpoint=%s)",
               path,ln_buf,::mountpoint.c_str());
      errno = ENOENT;
      free(ln_buf);
      return -1;
    }
  }
  else {
    // symlink is relative path
    buf = GetParentPath(buf);
    buf += "/";
    buf += ln_buf;
  }
  free(ln_buf);

  // In case the symlink references other symlinks or contains ".."
  // or "."  we must now call expand_path on the result.

  return expand_path(ctx, buf.c_str(), expanded_path, depth + 1);
}

/* Like expand_path(), but do not expand the final element of the path. */
static int expand_ppath(cvmfs_context  *ctx,
                        char const     *path,
                        string         &expanded_path)
{
  string p_path = GetParentPath(path);
  string fname = GetFileName(path);

  if( p_path == "" ) {
    expanded_path = path;
    return 0;
  }

  int rc = expand_path(ctx, p_path.c_str(), expanded_path);
  if( rc != 0 ) {
    return rc;
  }

  expanded_path += "/";
  expanded_path += fname;

  return 0;
}

int cvmfs_open(cvmfs_context *ctx, const char *path)
{
  string lpath;
  int rc;
  rc = expand_path(ctx, path, lpath);
  if( rc < 0 ) {
    return -1;
  }
  path = lpath.c_str();

  rc = ctx->Open(path);
  if (rc < 0) {
    errno = -rc;
    return -1;
  }
  return rc;
}

int cvmfs_close(cvmfs_context *ctx, int fd)
{
  int rc = ctx->Close(fd);
  if (rc < 0) {
    errno = -rc;
    return -1;
  }
  return 0;
}

int cvmfs_readlink(cvmfs_context *ctx, const char *path, char *buf, size_t size) {
  string lpath;
  int rc;
  rc = expand_ppath(ctx, path, lpath);
  if( rc < 0 ) {
    return -1;
  }
  path = lpath.c_str();

  rc = ctx->Readlink(path, buf, size);
  if (rc < 0) {
    errno = -rc;
    return -1;
  }
  return 0;
}

int cvmfs_stat(cvmfs_context *ctx, const char *path, struct stat *st)
{
  string lpath;
  int rc;
  rc = expand_path(ctx, path, lpath);
  if( rc < 0 ) {
    return -1;
  }
  path = lpath.c_str();

  rc = ctx->GetAttr(path,st);
  if( rc < 0 ) {
    errno = -rc;
    return -1;
  }
  return 0;
}

int cvmfs_lstat(cvmfs_context *ctx, const char *path,struct stat *st)
{
  string lpath;
  int rc;
  rc = expand_ppath(ctx, path, lpath);
  if( rc < 0 ) {
    return -1;
  }
  path = lpath.c_str();

  rc = ctx->GetAttr(path, st);
  if( rc < 0 ) {
    errno = -rc;
    return -1;
  }
  return 0;
}

int cvmfs_listdir(cvmfs_context *ctx, const char *path, char ***buf, size_t *buflen)
{
  string lpath;
  int rc;
  rc = expand_path(ctx, path, lpath);
  if( rc < 0 ) {
    return -1;
  }
  path = lpath.c_str();

  rc = ctx->ListDirectory(path, buf, buflen);
  if( rc < 0 ) {
    errno = -rc;
    return -1;
  }
  return 0;
}

cvmfs_context* cvmfs_attach_repo(char const *options)
{
  /* Parse options */
  repo_options opts;
  int parse_result = opts.parse_options(options);
  if (parse_result != 0)
  {
    if (parse_result < 0) {
      fprintf(stderr,"Invalid CVMFS options: %s.\n",options);
      usage();
    }
    return NULL;
  }
  if (opts.url.empty()) {
    fprintf(stderr,"No url specified in CVMFS repository options: %s.\n",options);
    return NULL;
  }

  return cvmfs_context::Create(opts);
}

int cvmfs_init(char const *options) {
  global_options opts;
  int parse_result = opts.parse_options(options);
  if (parse_result != 0) {
    fprintf(stderr, "Invalid CVMFS global options: %s.\n",options);
    usage();
    return parse_result;
  }

  return cvmfs_globals::Initialize(opts);
}

void cvmfs_fini(cvmfs_context *ctx) {
  delete ctx;
  ::mountpoint = "";
}

static void (*ext_log_fn)(const char *msg) = NULL;

static void libcvmfs_log_fn(const LogSource /*source*/, const int /*mask*/, const char *msg) {
	if( ext_log_fn ) {
		(*ext_log_fn)(msg);
	}
}

void cvmfs_set_log_fn( void (*log_fn)(const char *msg) )
{
	ext_log_fn = log_fn;
	if( log_fn == NULL ) {
		SetAltLogFunc( NULL );
	}
	else {
		SetAltLogFunc( libcvmfs_log_fn );
	}
}

int cvmfs_remount(cvmfs_context *ctx)
{
	catalog::LoadError retval = ctx->RemountStart();
	if( retval == catalog::kLoadNew || retval == catalog::kLoadUp2Date) {
		return 0;
	}
	return -1;
}
