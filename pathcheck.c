#define RSH_CMD			"cat "					/* Command for loading the image using RSH */

static char *
fnCheck(char *fn)
{
	if (!fn || !*fn) {
		fprintf(stderr,"No file; trying 'rtems.bin'\n");
		fn="rtems.bin";
	}
	return fn;
}

#ifndef COREDUMP_APP

static int
srvCheck(char **srvname, char *path)
{
	if (   !path || !*path || 0==strcmp(path, "BOOTP_HOST") ) {
		if ( !*srvname ) {
			fprintf(stderr,"No server name :-(\n");
			return -1;
		}
	} else {
		/* Changed server name */
		free(*srvname);
		*srvname = strdup(path);
	}
	return 0;
}

static int isNfsPath(char **srvname, char *opath, int *perrfd)
{
static char *lastmount = 0;

int  fd    = -1;
char *fn   = 0, *path = strdup(opath);
char *col1 = 0, *col2 = 0, *slas = 0, *at = 0, *srv=path, *ugid=0;

	*perrfd = -1;

	if ( 	!(col1=strchr(path,':'))
		 || !(col2=strchr(col1+1,':'))
		 || !(slas=strchr(path,'/'))
         || slas!=col1+1 ) {
		if (col1)
			fprintf(stderr,"NFS pathspec is [[uid.gid@]host]:<path_to_mount>:<file_rel_to_mntpt>\n");
		free( path );
		return -2;
	}

	if ( (at = strchr(path,'@')) && at < col1 ) {
		srv = at + 1;
		ugid = path;
	} else {
		at = 0;
		srv = path;
	}

	if ( lastmount ) {
		unmount(lastmount);
		free(lastmount);
		lastmount = 0;
	} else if ( !nfsInited ) {
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

	if ( srvCheck( srvname, srv ) )
		goto cleanup;

	fn = fnCheck( col2 + 1);

	lastmount = malloc( ( ugid ? strlen(ugid) + 1 : 0 ) + strlen(*srvname)+2 );
	sprintf(lastmount,"/%s%s%s",ugid ? ugid : "", at ? "@" : "", *srvname);
	if ( nfsMount(lastmount+1, col1+1, lastmount) ) {
		unlink(lastmount);
		free(lastmount);
		lastmount = 0;
	} else {
		char *tmppath = malloc(strlen(lastmount) + strlen(fn) + 3);
		sprintf(tmppath,"%s/%s",lastmount, fn);
		fd = open(tmppath,O_RDONLY);
		free(tmppath);
		if ( fd < 0 ) {
			perror("Opening boot file failed");
		}
	}

cleanup:
	free(path);
	return fd;
}

extern int rcmd();

#define RSH_PORT	514
#define RSH_BUFSZ	25000000

static int isRshPath(char **srvname, char *opath, int *perrfd)
{
int	fd = -1;
char *username, *fn;
char *tild = 0, *col1 = 0;
char *cmd  = 0;
char *path = 0;

	fn = path = strdup(opath);

	col1 = strchr(path,':');
	tild = strchr(path,'~');

	/* sanity check */
	if ( !tild ||                       /* no tilde */
		 (col1 && tild != col1 + 1) ||  /* found colon but not :~ */
		 (!col1 && (!*srvname ||        /* no colon and no dflt server */
                     tild != path)      /* no colon and path doesn't start with tilde */
		 ) ) {
		goto cleanup;
	}

	*perrfd = -1;

	if (col1) {
		*col1 = 0;
	}

	if ( srvCheck( srvname, path ) )
		goto cleanup;
	
	/* they gave us user name */
	fn = tild;
	username=++fn;
	if ((fn=strchr(fn,'/'))) {
		*(fn++)=0;
	}

	fprintf(stderr,"Loading as '%s' from '%s' using RSH\n",username, *srvname);

	fn = fnCheck(fn);

	/* cat filename to command */
	cmd = malloc(strlen(RSH_CMD)+strlen(fn)+1);
	sprintf(cmd,"%s%s",RSH_CMD,fn);

	fd=rcmd(srvname,RSH_PORT,username,username,path,perrfd);

	if (fd<0) {
		fprintf(stderr,"rcmd (%s): got no remote stdout descriptor\n",
						strerror(errno));
		if ( *perrfd >= 0 )
			close( *perrfd );
		*perrfd = -1;
	} else if ( *perrfd<0 ) {
		fprintf(stderr,"rcmd (%s): got no remote stderr descriptor\n",
						strerror(errno));
		if ( fd>=0 )
			close( fd );
		fd = -1;
	}
cleanup:
	free( path );
	free( cmd  );
	return fd;
}
#endif

static int isTftpPath(char **srvname, char *opath, int *perrfd)
{
int        fd = -1;
char       *path, *fn;


	*perrfd = -1;

 	if (!tftpInited && rtems_bsdnet_initialize_tftp_filesystem()) {
		fprintf(stderr,"TFTP FS initialization failed - try NFS or RSH\n");
		return -1;
	} else
		tftpInited=1;

	path = strdup(opath);

	fprintf(stderr,"Using TFTP for transfer\n");
	if (strncmp(path,"/TFTP/",6)) {
		path=realloc(path,strlen(tftp_prefix)+strlen(path)+1);
		sprintf(path,"%s%s",tftp_prefix,opath);
		fn = opath;
	} else {
		char *tmp;
		fn = path + 6;
		/* may be necessary to rebuild the server name */
		if ((tmp=strchr(fn,'/'))) {
			*tmp=0;
			free(*srvname);
			*srvname=strdup(path+6);
			*tmp='/';
			fn = tmp + 1;
		}
	}
	if ( !fn || !*fn ) {
		fn = fnCheck(fn);
		path = realloc(path, strlen(path) + strlen(fn) + 1 );
		strcat(path,fn);
	}
	if ((fd=open(path,TFTP_OPEN_FLAGS,0))<0) {
			fprintf(stderr,"unable to open %s\n",path);
	}
	free(path);
	return fd;
}
