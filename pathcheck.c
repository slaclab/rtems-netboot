
#ifndef COREDUMP_APP
static int isNfsPath(char **srvname, char *path, int *perrfd)
{
static char *lastmount = 0;
int        fd = -1;

char *col1 = 0, *col2 = 0, *slas = 0, *at = 0, *srv=path, *ugid=0;

	*perrfd = -1;

	if ( 	!(col1=strchr(path,':'))
		 || !(col2=strchr(col1+1,':'))
		 || !(slas=strchr(path,'/'))
         || slas!=col1+1 ) {
		if (col1)
			fprintf(stderr,"NFS pathspec is [[uid.gid@]host]:<path_to_mount>:<file_rel_to_mntpt>\n");
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
			return -1;
		}
		nfsInit(0,0);
		nfsInited = 1;
	}

	/* clear all separators */
	if ( at )
		*at = 0;
	*col1 = 0;
	*col2 = 0;

	if (   col1 == srv
		|| 0==strcmp(srv, "BOOTP_HOST") ) {
		if ( !*srvname ) {
			fprintf(stderr,"No server name :-(\n");
			goto cleanup;
		}
	} else {
		/* Changed server name */
		free(*srvname);
		*srvname = strdup(srv);
	}
	lastmount = malloc( ( ugid ? strlen(ugid) + 1 : 0 ) + strlen(*srvname)+2 );
	sprintf(lastmount,"/%s%s%s",ugid ? ugid : "", at ? "@" : "", *srvname);
	if ( nfsMount(lastmount+1, col1+1, lastmount) ) {
		unlink(lastmount);
		free(lastmount);
		lastmount = 0;
	} else {
		char *tmppath = malloc(strlen(lastmount) + strlen(col2+1) + 3);
		sprintf(tmppath,"%s/%s",lastmount,col2+1);
		fd = open(tmppath,O_RDONLY);
		free(tmppath);
		if ( fd < 0 ) {
			perror("Opening boot file failed");
		}
	}

cleanup:
	*col1 = *col2 = ':';
	if ( at )
		*at = '@';
	return fd;
}

extern int rcmd();

#define RSH_PORT	514
#define RSH_BUFSZ	25000000

static int isRshPath(char **srvname, char *path, int *perrfd)
{
int	fd = -1;
char *username, *fn;
char *chpt = *srvname;

	fn = path = strdup(path);

	*perrfd = -1;

	/* they gave us user name */
	username=++fn;
	if ((fn=strchr(fn,'/'))) {
		*(fn++)=0;
	}

	fprintf(stderr,"Loading as '%s' using RSH\n",username);

	if (!fn || !*fn) {
		fprintf(stderr,"No file; trying 'rtems.bin'\n");
		fn="rtems.bin";
	}

	/* cat filename to command */
	path=realloc(path,strlen(RSH_CMD)+strlen(fn)+1);
	sprintf(path,"%s%s",RSH_CMD,fn);

	fd=rcmd(&chpt,RSH_PORT,username,username,path,perrfd);

	free(path);

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
	return fd;
}
#endif

static int isTftpPath(char **srvname, char *opath, int *perrfd)
{
int        fd = -1;
char       *path;


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
	} else {
		char *tmp;
		/* may be necessary to rebuild the server name */
		if ((tmp=strchr(path+6,'/'))) {
			*tmp=0;
			free(*srvname);
			*srvname=strdup(path+6);
			*tmp='/';
		}
	}
	if ((fd=open(path,TFTP_OPEN_FLAGS,0))<0) {
			fprintf(stderr,"unable to open %s\n",path);
	}
	free(path);
	return fd;
}
