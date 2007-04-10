typedef struct fake_bsdnet_config_ {
	char *hostname;
	char *domainname;
	char *log_host;
	char *gateway;
	char *name_server[3];
	char *ntp_server[3];
} Fake_bsdnet_config;

Fake_bsdnet_config rtems_bsdnet_config;

char theNvram[2048];

#ifdef NVRAM_READONLY
#ifndef NVRAM_GETVAR
#define NVRAM_GETVAR(n) getenv(n)
#endif
#else
#define BSP_NVRAM_BOOTPARMS_START theNvram
#define BSP_NVRAM_BOOTPARMS_END   (theNvram + sizeof(theNvram))
#endif

struct in_addr rtems_bsdnet_bootp_server_address;
char *rtems_bsdnet_bootp_server_name;
char *rtems_bsdnet_bootp_boot_file_name;
char *rtems_bsdnet_bootp_cmdline;

char * rtems_bsdnet_domain_name;
struct in_addr rtems_bsdnet_log_host_address;

struct in_addr dummy1[3];
struct in_addr *rtems_bsdnet_nameserver=dummy1;
int rtems_bsdnet_nameserver_count;

struct in_addr dummy2[3];
struct in_addr *rtems_bsdnet_ntpserver=dummy2;
int rtems_bsdnet_ntpserver_count;

				
static inline int rtems_str2ifmedia(char *med, int xx)
{
	return -1;
}
