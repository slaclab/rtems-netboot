#define RSH_CMD			"cat "					/* Command for loading the image using RSH */

#define DFLT_FNAME		dflt_fname

static char *dflt_fname = "rtems.bin";

/* A couple of words about the isXXXPath() interface:
 *
 * isXXXPath( pServerName, pathspec, pErrfd, pThePath [, pTheMountPt] )
 *
 * pServerName: A malloced string specifying a the default server.
 * (IN/OUT)     This can be overridden by a host/server present in
 *              'pathspec'. In this case, *pServerName is reallocated()
 *              and updated with the actual server name.
 * 
 * pathspec:    A path specifier defining user, host and file names; see
 * (IN)         below.
 * 
 * pErrfd:      Pointer to an integer where a file descriptor for an
 * (IN/OUT)     error stream can be stored (only relevant for rcmd/rsh
 *              the other methods, i.e., NFS and TFTP set *pErrfd to -1).
 *
 *              Passing a NULL value for pErrfd is valid and instructs
 *              the routines to merely check parameters and build
 *              *pThePath and *pTheMountPt. No rcmd/nfsMount/ and/or
 *              file opening is attempted.
 *
 * pThePath:    If non-NULL, the canonicalized pathspec string is returned
 * (IN/OUT)     in *pThePath. I.e., the default server and filename etc.
 *              have been substituted. The caller is responsible for 
 *              free()ing this string;
 *
 * pTheMountPt: (NFS only) If the address of a non-empty string is passed,
 * (IN/OUT)     the string defines the mount point to be used (possibly
 *              created by 'nfsMount()'). If an empty string is passed,
 *              a default mount-point name is constructed according to the
 *              syntax:
 *                     [ <uid> '.' <gid> '@' ] <host> ':' <exported_path>
 *              NOTE: all '/' chars in <exported_path> are substitued by
 *                    '.'; trailing '/' are stripped. 
 *              It is the caller's responsibility to free() the string.
 *
 * RETURNS:     SUCCESS:
 *                 If pErrfd was NULL: 0 on success (valid path).
 *
 *                 If pErrfd was non-NULL: open file descriptor (integer >= 0)
 *                 The caller can read from the returned fd and must close it
 *                 eventually.
 *
 *              FAILURE: (value < 0)
 *                  < -10 --> invalid pathspec for this method.
 *                    -10 --> pathspec defined no host and no default
 *                            server passed (*pServerName empty).
 *                    -2  --> (NFS only) mount was successful but
 *                            file couldn't be opened. NFS is still mounted.
 *                  other --> NFS mount failed (NFS)
 *                            file couldn't be opened (others).
 *              
 *              NOTES: *pThePath and *pTheMountp are possibly still set
 *                     even if return value < 0.
 *
 *                     *pServerName can be modified even if return value < 0.
 *
 */

static char *
fnCheck(char *path)
{
char *rval = malloc( (path ? strlen(path) : 0 )+strlen(DFLT_FNAME) + 1);
char *fn   = 0;

	if ( !path || !*path || '/' == path[strlen(path)-1] ) {
		fprintf(stderr,"No file; trying '%s'\n",DFLT_FNAME);
		fn=DFLT_FNAME;
	}
	sprintf(rval,"%s%s",path ? path : "", fn ? fn : "");
	return rval;
}

#ifndef COREDUMP_APP

static int
srvCheck(char **srvname, char *path)
{
	if (   !path || !*path || 0==strcmp(path, "BOOTP_HOST") ) {
		if ( !*srvname ) {
			fprintf(stderr,"No server name in pathspec and default server not set :-(\n");
			return -1;
		}
	} else {
		/* Changed server name */
		free(*srvname);
		*srvname = strdup(path);
	}
	return 0;
}

static void releaseMount(char **mountp)
{
    if ( *mountp ) {
        unmount( *mountp );
        unlink( *mountp );
        free( *mountp );
        *mountp = 0;
    }
}

/* RETURNS -2 if mount is ok but file cannot be opened; leaves NFS mounted */
static int isNfsPath(char **srvname, char *opath, int *perrfd, char **thepathp, char **mntp, char **thesrvp)
{

int  fd    = -1, l;
char *fn   = 0, *path = strdup(opath);
char *col1 = 0, *col2 = 0, *slas = 0, *at = 0, *srv=path, *ugid=0;
char *srvpart  = 0;


	if ( perrfd )
		*perrfd = -1;

	if ( 	!(col1=strchr(path,':'))
		 || !(col2=strchr(col1+1,':'))
		 || !(slas=strchr(path,'/'))
         || slas!=col1+1 ) {
		if (col1)
			fprintf(stderr,"NFS pathspec is [[uid.gid@]host]:<path_to_mount>:<file_rel_to_mntpt>\n");
		free( path );
		return -11;
	}

	if ( (at = strchr(path,'@')) && at < col1 ) {
		srv = at + 1;
		ugid = path;
	} else {
		at = 0;
		srv = path;
	}

	if ( !nfsInited ) {
		if ( rpcUdpInit() ) {
			fprintf(stderr,"RPC-IO initialization failed - try RSH or TFTP\n");
			goto cleanup;
		}
		nfsInit(0,0);
		nfsInited = 1;
	}

	/* clear all separators */
	if ( at )
		*at = 0;
	*col1 = 0;
	*col2 = 0;

	if ( srvCheck( srvname, srv ) ) {
		fd = -10;
		goto cleanup;
	}

	fn = fnCheck( col2 + 1);

	l = ( ugid ? strlen(ugid) + 1: 0 )     /* uid.gid@ */ +
		strlen(*srvname)                /* host     */ ;

	srvpart = malloc ( l + 1 );
	if ( ugid ) {
		strcpy(srvpart,ugid);
		strcat(srvpart,"@");
	} else {
		*srvpart = 0;
	}
	strcat(srvpart, *srvname);

	if ( !*mntp ) {
		char *tmp;

		*mntp = malloc( 1 /* '/' */ + l + 1 /* ':' */ + strlen(col1+1) + 1 );

		sprintf(*mntp,"/%s:%s", srvpart, col1+1);

		/* remove trailing '/' in *mntp */
		for ( tmp = *mntp + strlen(*mntp) - 1; '/' == *tmp && tmp >= *mntp; )
				*tmp-- = 0;

		/* convert '/' in the server path into '.' */
		for ( tmp = *mntp + 1 + l + 1; (tmp = strchr(tmp, '/')); ) {
			*tmp++ = '.';
		}
	} else {
		/* they provide a mountpoint */
	}

	path = realloc(path, strlen(*mntp) + 1 /* '/' */ + strlen(fn) + 1);
	sprintf(path,"%s/%s",*mntp,fn);

	if ( perrfd ) {
		struct stat probe;
		int         existed;

		/* race condition from here till possible unlink */
		existed = !stat(*mntp, &probe);
		
		if ( nfsMount(srvpart, col1+1, *mntp) ) {
			if ( !existed )
				unlink(*mntp);
			free(*mntp);
			*mntp = 0;
			goto cleanup;
		} else {
			fd = open(path,O_RDONLY);
			if ( fd < 0 ) {
				perror("Opening boot file failed");
				/* leave mounted and continue */
				fd = -2;
			}
		}
	} else {
		/* Don't actually do anything but signal success */
		fd = 0;
	}

	/* are they interested in the path ? */
	if ( thepathp ) {
		*thepathp = path; 
		path = 0;
	}

	if ( thesrvp )  {
		*thesrvp  = srvpart;
		srvpart   = 0;
	}

cleanup:
	free(srvpart);
	free(fn);
	free(path);
	return fd;
}

extern int rcmd();

#define RSH_PORT	514
#define RSH_BUFSZ	25000000

static int isRshPath(char **srvname, char *opath, int *perrfd, char **thepathp)
{
int	fd = -1;
char *username, *fn = 0, *tmp;
char *tild = 0, *col1 = 0;
char *cmd  = 0;
char *path = 0;

	path = strdup(opath);

	col1 = strchr(path,':');
	tild = strchr(path,'~');

	if ( perrfd )
		*perrfd = -1;

	/* sanity check */
	if ( !tild ||                       /* no tilde */
		 (col1 && tild != col1 + 1) ||  /* found colon but not :~ */
		 (!col1 && tild != path)      /* no colon and path doesn't start with tilde */
	   ) {
		fd = -11;
		goto cleanup;
	}

	if ( !col1 && !*srvname ) {
		fprintf(stderr,"No default server; specify '<server>:~<user>/<path'\n");
		fd = -10;
		goto cleanup;
	}

	if (col1) {
		*col1 = 0;
	}
	*tild = 0;

	/* they gave us user name */
	username=tild+1;

	if ((tmp=strchr(username,'/'))) {
		*(tmp++)=0;
	}

	if ( srvCheck( srvname, path ) )
		goto cleanup;
	
	fprintf(stderr,"Loading as '%s' from '%s' using RSH\n",username, *srvname);

	fn = fnCheck(tmp);

	/* cat filename to command */
	cmd = malloc(strlen(RSH_CMD)+strlen(fn)+1);
	sprintf(cmd,"%s%s",RSH_CMD,fn);

	fd=rcmd(srvname,RSH_PORT,username,username,cmd,perrfd);

	if ( perrfd ) {
		if (fd<0) {
			fprintf(stderr,"rcmd"/* (%s)*/": got no remote stdout descriptor\n"
							/* ,strerror(errno)*/);
			if ( *perrfd >= 0 )
				close( *perrfd );
			*perrfd = -1;
		} else if ( *perrfd<0 ) {
			fprintf(stderr,"rcmd"/* (%s)*/": got no remote stderr descriptor\n"
							/* ,strerror(errno)*/);
			if ( fd>=0 )
				close( fd );
			fd = -1;
		}
	} else {
		/* don't actually do anything but signal success */
		fd = 0;
	}

	/* are they interested in the path ? */
	if ( thepathp ) {
		*thepathp = malloc(strlen(*srvname) + 2 + strlen(username) + 1 + strlen(fn) + 1);
		sprintf(*thepathp,"%s:~%s/%s",*srvname,username,fn);
	}

cleanup:
	free( fn   );
	free( path );
	free( cmd  );
	return fd;
}
#endif

static int isTftpPath(char **srvname, char *opath, int *perrfd, char **thepathp)
{
int     fd = -1, hasPrefix;
char    *path    = 0, *fn = 0, *srvpart = 0, *slash=0;
char	*ofn;


	if ( perrfd )
		*perrfd = -1;

	hasPrefix = !strncmp(opath,"/TFTP/",6);

	if ( !hasPrefix && '/'==*opath ) {
		/* must not be an absolute path with no prefix */
		return -11;
	}

 	if (!tftpInited && rtems_bsdnet_initialize_tftp_filesystem()) {
		fprintf(stderr,"TFTP FS initialization failed - try NFS or RSH\n");
		goto cleanup;
	} else
		tftpInited=1;


	fprintf(stderr,"Using TFTP for transfer\n");

	srvpart = strdup(opath);

	if ( !hasPrefix ) {

		/* no TFTP prefix; set srvpart to tftp prefix and strip trailing '/' */

		srvpart = strdup(tftp_prefix); /* tftp_prefix contains trailing '/' */
		ofn = opath;
		if ( *srvpart )
			srvpart[strlen(srvpart)-1] = 0; /* strip it */
	} else {
		/* have TFTP prefix; break at first slash after /TFTP/,
		 * copy header to srvpart and remember trailing path
		 * also, *srvname has to be reassigned.
		 */
		ofn = srvpart + 6;
		/* may be necessary to rebuild the server name */
		if ((slash=strchr(ofn,'/'))) {
			*slash=0;	/* break into two pieces */
			/* reassign *srvname */
			free(*srvname);
			*srvname=strdup(ofn);
			/* ofn points to the path on the server */
			ofn = slash + 1;
		} else {
			srvpart[5]=0;
		}
	}
	fn = fnCheck(ofn);
	path = malloc(strlen(srvpart) + 1 /* '/' */ + strlen(fn) + 1);
	sprintf(path,"%s/%s",srvpart,fn);

	if ( perrfd ) {
		if ((fd=open(path,TFTP_OPEN_FLAGS,0))<0) {
			fprintf(stderr,"unable to open %s\n",path);
		}
	} else {
		/* don't actually do anything but signal success */
		fd = 0;
	}

	/* are they interested in the path ? */
	if ( thepathp ) {
		*thepathp = path; 
		path = 0;
	}

cleanup:
	free(srvpart);
	free(fn);
	free(path);
	return fd;
}
