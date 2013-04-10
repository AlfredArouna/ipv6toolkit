/*
 * address6: A tool to decode IPv6 addresses
 *
 * Copyright (C) 2013 Fernando Gont (fgont@si6networks.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * 
 * Build with: gcc address6.c -Wall -o address6
 * 
 * This program has been tested to compile and run on: Debian GNU/Linux 6.0,
 * FreeBSD 9.0, NetBSD 5.1, OpenBSD 5.0, and Ubuntu 11.10.
 *
 * It requires that the libpcap library be installed on your system.
 *
 * Please send any bug reports to Fernando Gont <fgont@si6networks.com>
 */

#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/resource.h>
#include <pwd.h>
#include <sys/param.h>
#include "address6.h"
#include "ipv6toolkit.h"

void					usage(void);
void					print_help(void);
int						read_prefix(char *, unsigned int, char **);
size_t					Strnlen(const char *, size_t);
unsigned int			is_service_port(u_int16_t);
unsigned int			zero_byte_iid(struct in6_addr *);
void					decode_ipv6_address(struct decode6 *, struct stats6 *);
void					print_dec_address_script(struct decode6 *);
int						init_host_list(struct host_list *);
u_int16_t				key(struct host_list *, struct in6_addr *);
struct host_entry *		add_host_entry(struct host_list *, struct in6_addr *);
unsigned int			is_ip6_in_list(struct host_list *, struct in6_addr *);
int 					is_eq_in6_addr(struct in6_addr *, struct in6_addr *);
void					print_stats(struct stats6 *);

unsigned char			stdin_f=0, addr_f=0, verbose_f=0, decode_f=0, print_unique_f=0, stats_f=0;
char					line[MAX_LINE_SIZE];

int main(int argc, char **argv){
	extern char			*optarg;	
	extern int			optind;
	struct decode6		addr;
	struct stats6		stats;
	struct host_list	hlist;
	int					r;
	char				*ptr;
	char				pv6addr[INET6_ADDRSTRLEN];
	uid_t				ruid;
	gid_t				rgid;
	struct passwd		*pwdptr;

	static struct option longopts[] = {
		{"address", required_argument, 0, 'a'},
		{"stdin", no_argument, 0, 'i'},
		{"decode", no_argument, 0, 'd'},
		{"print-stats", no_argument, 0, 's'},
		{"print-unique", no_argument, 0, 'q'},
		{"block", required_argument, 0, 'j'},
		{"accept", required_argument, 0, 'b'},
		{"verbose", no_argument, 0, 'v'},
		{"help", no_argument, 0, 'h'}
	};

	char shortopts[]= "a:idsqj:b:vh";

	char option;

	if(argc<=1){
		usage();
		exit(1);
	}

	/* 
	   address6 does not need superuser privileges. But since most of the other tools in the toolkit do,
	   the user might unnecessarily run it as such. We release any unnecessary privileges before proceeding
	   further.

	   If the real UID is not root, we setuid() and setgid() to that user and group, releasing superuser
	   privileges. Otherwise, if the real UID is 0, we try to setuid() to "nobody", releasing superuser 
	   privileges.
	 */
	if( (ruid=getuid()) && (rgid=getgid())){
		if(setgid(rgid) == -1){
			puts("Error while releasing superuser privileges (changing to real GID)");
			exit(1);
		}

		if(setuid(ruid) == -1){
			puts("Error while releasing superuser privileges (changing to real UID)");
			exit(1);
		}
	}
	else{
		if((pwdptr=getpwnam("nobody"))){
			if(pwdptr->pw_uid && (setgid(pwdptr->pw_gid) == -1)){
				puts("Error while releasing superuser privileges (changing to nobody's group)");
				exit(1);
			}

			if(pwdptr->pw_uid && (setuid(pwdptr->pw_uid) == -1)){
				puts("Error while releasing superuser privileges (changing to 'nobody')");
				exit(1);
			}
		}
	}

	while((option=getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
		switch(option) {
			case 'a':
				if( inet_pton(AF_INET6, optarg, &(addr.ip6)) <= 0){
					puts("inet_pton(): address not valid");
					exit(1);
				}
		
				addr_f = 1;
				break;

			case 'i':  /* Read from stdin */
				stdin_f=1;
				break;
	    
			case 'd':	/* IPv6 Destination Address */
				decode_f = 1;
				break;

			case 's':	/* IPv6 Destination Address */
				stats_f = 1;
				break;

			case 'q':	/* IPv6 Destination Address */
				print_unique_f = 1;
				break;

			case 'v':	/* IPv6 Destination Address */
				verbose_f++;
				break;

			default:
				usage();
				exit(1);
				break;
		
		} /* switch */
	} /* while(getopt) */


	if(stdin_f && addr_f){
		puts("Cannot specify both '-a' and '-s' at the same time (try only one of them at a time)");
		exit(1);
	}

	if(!stdin_f && !addr_f){
		puts("Must specify an IPv6 address with '-a', or set '-s' to read addresses from stdin");
		exit(1);
	}

	if(stats_f && !stdin_f){
		puts("Cannot obtain statistics based on a single IPv6 address (should be using '-i')");
		exit(1);
	}

	/* By default, address6 decodes IPv6 addresses */
	if(!print_unique_f && !stats_f)
		decode_f=1;

	if(print_unique_f){
		if(!init_host_list(&hlist)){
			puts("Not enough memory when initializing internal host list");
			exit(1);
		}
	}

	if(stats_f){
		memset(&stats, 0, sizeof(stats));
	}

	if(stdin_f){
		while(fgets(line, MAX_LINE_SIZE, stdin) != NULL){
			r= read_prefix(line, Strnlen(line, MAX_LINE_SIZE), &ptr);

			if(r==1){
				if ( inet_pton(AF_INET6, ptr, &(addr.ip6)) <= 0){
					if(decode_f)
						puts("Error: Invalid IPv6 address");

					continue;
				}

				if(print_unique_f){
					if(is_ip6_in_list(&hlist, &(addr.ip6))){
						continue;
					}
					else{
						if(add_host_entry(&hlist, &(addr.ip6)) == NULL){
							puts("Not enough memory (or hit internal artificial limit) when storing IPv6 address in memory");
							exit(1);
						}
					}
				}

				/* If we were not asked to decode the address, we should print it on stdout */
				if(decode_f || stats_f){
					decode_ipv6_address(&addr, &stats);

					if(decode_f)
						print_dec_address_script(&addr);
				}
				else{
					if(inet_ntop(AF_INET6, &(addr.ip6), pv6addr, sizeof(pv6addr))<=0){
						puts("inet_ntop(): Error converting IPv6 address to presentation format");
						exit(1);
					}

					printf("%s\n", pv6addr);
				}
			}
		}

		if(stats_f){
			print_stats(&stats);
		}
	}

	exit(0);
}


/*
 * Function: read_prefix()
 *
 * Obtain a pointer to the beginning of non-blank text, and zero-terminate that text upon space or comment.
 */

int read_prefix(char *line, unsigned int len, char **start){
	char *end;

	*start=line;

	while( (*start < (line + len)) && (**start==' ' || **start=='\t' || **start=='\r' || **start=='\n')){
		(*start)++;
	}

	if( *start == (line + len))
		return(0);

	if( **start == '#')
		return(0);

	end= *start;

	while( (end < (line + len)) && !(*end==' ' || *end=='\t' || *end=='#' || *end=='\r' || *end=='\n'))
		end++;

	*end=0;
	return(1);
}


/*
 * Function: is_service_port()
 *
 * Check whether a short int is in the list of service ports (in hexadecmal or decimal "notations")
 */

unsigned int is_service_port(u_int16_t port){
	unsigned int 	i;
	u_int16_t		service_ports_hex[]={0x21, 0x22, 0x23, 0x25, 0x49, 0x53, 0x80, 0x110, 0x123, 0x179, 0x220, 0x389, \
						                 0x443, 0x547, 0x993, 0x995, 0x1194, 0x3306, 0x5060, 0x5061, 0x5432, 0x6446, 0x8080};
	u_int16_t		service_ports_dec[]={21, 22, 23, 25, 49, 53, 80, 110, 123, 179, 220, 389, \
						                 443, 547, 993, 995, 1194, 3306, 5060, 5061, 5432, 6446, 8080};

	
	for(i=0; i< (sizeof(service_ports_hex)/sizeof(u_int16_t)); i++){
		if(port == service_ports_hex[i])
			return(1);
	}

	for(i=0; i< (sizeof(service_ports_hex)/sizeof(u_int16_t)); i++){
		if(port == service_ports_dec[i])
			return(1);
	}

	return(0);
}

/*
 * Function: zero_byte_iid()
 *
 * Counts the number of zero-bytes in an IPv6 Interface ID
 */

unsigned int zero_byte_iid(struct in6_addr *ipv6){
	unsigned int i, nonzero=0;

	for(i=8; i<=15; i++){
		if(ipv6->s6_addr[i] == 0)
			nonzero++;
	}

	return(nonzero);
}


/*
 * Function: decode_ipv6_address()
 *
 * Decodes/analyzes an IPv6 address
 */

void decode_ipv6_address(struct decode6 *addr, struct stats6 *stats){
	u_int16_t	scope;

	(stats->total)++;

	if(IN6_IS_ADDR_UNSPECIFIED(&(addr->ip6))){
		addr->type= IPV6_UNSPEC;
		addr->subtype= IPV6_UNSPEC;
		addr->scope= SCOPE_UNSPECIFIED;
		(stats->ipv6unspecified)++;
	}
	else if(IN6_IS_ADDR_MULTICAST(&(addr->ip6))){
		addr->type= IPV6_MULTICAST;
		addr->iidtype= IID_UNSPECIFIED;
		addr->iidsubtype= IID_UNSPECIFIED;
		(stats->ipv6multicast)++;

		if((addr->ip6.s6_addr16[0] & htons(0xff00)) == htons(0xff00)){
			if((addr->ip6.s6_addr16[0] & htons(0xfff0)) == htons(0xff00)){
				addr->subtype= MCAST_PERMANENT;
				(stats->mcastpermanent)++;
			}
			else if((addr->ip6.s6_addr16[0] & htons(0xfff0)) == htons(0xff10)){
				addr->subtype= MCAST_NONPERMANENT;
				(stats->mcastnonpermanent)++;
			}
			else if((addr->ip6.s6_addr16[0] & htons(0xfff0)) == htons(0xff20)){
				addr->subtype= MCAST_INVALID;
				(stats->mcastinvalid)++;
			}
			else if((addr->ip6.s6_addr16[0] & htons(0xfff0)) == htons(0xff30)){
				addr->subtype= MCAST_UNICASTBASED;
				(stats->mcastunicastbased)++;
			}
			else if((addr->ip6.s6_addr16[0] & htons(0xfff0)) == htons(0xff40)){
				addr->subtype= MCAST_INVALID;
				(stats->mcastinvalid)++;
			}
			else if((addr->ip6.s6_addr16[0] & htons(0xfff0)) == htons(0xff50)){
				addr->subtype= MCAST_INVALID;
				(stats->mcastinvalid)++;
			}
			else if((addr->ip6.s6_addr16[0] & htons(0xfff0)) == htons(0xff60)){
				addr->subtype= MCAST_INVALID;
				(stats->mcastinvalid)++;
			}
			else if((addr->ip6.s6_addr16[0] & htons(0xfff0)) == htons(0xff70)){
				addr->subtype= MCAST_EMBEDRP;
				(stats->mcastembedrp)++;
			}

			scope= htons(addr->ip6.s6_addr16[0]) & 0x000f;

			switch(scope){
				case 0:
					addr->scope= SCOPE_RESERVED;
					(stats->mscopereserved)++;
					break;

				case 1:
					addr->scope= SCOPE_INTERFACE;
					(stats->mscopeinterface)++;
					break;

				case 2:
					addr->scope= SCOPE_LINK;
					(stats->mnscopelink)++;
					break;

				case 3:
					addr->scope= SCOPE_RESERVED;
					(stats->mscopereserved)++;
					break;

				case 4:
					addr->scope= SCOPE_ADMIN;
					(stats->mscopeadmin)++;
					break;

				case 5:
					addr->scope= SCOPE_SITE;
					(stats->mscopesite)++;
					break;

				case 8:
					addr->scope= SCOPE_ORGANIZATION;
					(stats->mscopeorganization)++;
					break;

				case 0Xe:
					addr->scope= SCOPE_GLOBAL;
					(stats->mscopeglobal)++;
					break;

				default:
					addr->scope= SCOPE_UNASSIGNED;
					(stats->mscopeunassigned)++;
					break;
			}
		}
		else{
			addr->subtype= MCAST_UNKNOWN;
			(stats->mcastunknown)++;
		}
	}
	else{
		addr->type= IPV6_UNICAST;
		addr->iidtype= IID_UNSPECIFIED;
		addr->iidsubtype= IID_UNSPECIFIED;
		(stats->ipv6unicast)++;

		if(IN6_IS_ADDR_LOOPBACK(&(addr->ip6))){
			addr->subtype= UCAST_LOOPBACK;
			addr->scope= SCOPE_INTERFACE;
			(stats->ucastloopback)++;
		}
		else if(IN6_IS_ADDR_V4MAPPED(&(addr->ip6))){
			addr->subtype= UCAST_V4MAPPED;
			addr->scope= SCOPE_UNSPECIFIED;
			(stats->ucastv4mapped)++;
		}
		else if(IN6_IS_ADDR_V4COMPAT(&(addr->ip6))){
			addr->subtype= UCAST_V4COMPAT;
			addr->scope= SCOPE_UNSPECIFIED;
			(stats->ucastv4compat)++;
		}
		else if(IN6_IS_ADDR_LINKLOCAL(&(addr->ip6))){
			addr->subtype= UCAST_LINKLOCAL;
			addr->scope= SCOPE_LINK;
			(stats->ucastlinklocal)++;
		}
		else if(IN6_IS_ADDR_SITELOCAL(&(addr->ip6))){
			addr->subtype= UCAST_SITELOCAL;
			addr->scope= SCOPE_SITE;
			(stats->ucastsitelocal)++;
		}
		else if(IN6_IS_ADDR_UNIQUELOCAL(&(addr->ip6))){
			addr->subtype= UCAST_UNIQUELOCAL;
			addr->scope= SCOPE_GLOBAL;
			(stats->ucastuniquelocal)++;
		}
		else if(IN6_IS_ADDR_6TO4(&(addr->ip6))){
			addr->subtype= UCAST_6TO4;
			addr->scope= SCOPE_GLOBAL;
			(stats->ucast6to4)++;
		}
		else if(IN6_IS_ADDR_TEREDO(&(addr->ip6)) || IN6_IS_ADDR_TEREDO_LEGACY(&(addr->ip6))){
			addr->subtype= UCAST_TEREDO;
			addr->scope= SCOPE_GLOBAL;
			(stats->ucastteredo)++;
		}
		else{
			addr->subtype= UCAST_GLOBAL;
			addr->scope= SCOPE_GLOBAL;
			(stats->ucastglobal)++;
		}

		if(addr->subtype==UCAST_GLOBAL || addr->subtype==UCAST_V4MAPPED || addr->subtype==UCAST_V4COMPAT || \
			addr->subtype==UCAST_LINKLOCAL || addr->subtype==UCAST_SITELOCAL || addr->subtype==UCAST_UNIQUELOCAL ||\
			addr->subtype == UCAST_6TO4){

			if( (addr->ip6.s6_addr32[2] & htonl(0x020000ff)) == htonl(0x020000ff) && 
				(addr->ip6.s6_addr32[3] & htonl(0xff000000)) == htonl(0xfe000000)){
				addr->iidtype= IID_MACDERIVED;
				addr->iidsubtype= (ntohl(addr->ip6.s6_addr32[2]) >> 8) & 0xfffdffff;
				(stats->iidmacderived)++;
			}
			else if((addr->ip6.s6_addr32[2] & htonl(0xfdffffff)) == htonl(0x00005efe)){
				/* We assume the u bit can be o o 1, but the i/g bit must be 0 */
				addr->iidtype= IID_ISATAP;
				(stats->iidisatap)++;
			}
			else if(addr->ip6.s6_addr32[2] == 0 && (addr->ip6.s6_addr16[6] & htons(0xff00)) != 0 && addr->ip6.s6_addr16[7] != 0){
				addr->iidtype= IID_EMBEDDEDIPV4;
				(stats->iidmbeddedipv4)++;
			}
			else if(addr->ip6.s6_addr32[2] == 0 && \
			          ((addr->ip6.s6_addr16[6] & htons(0xff00)) == 0 && is_service_port(ntohs(addr->ip6.s6_addr16[7])))){
				addr->iidtype= IID_EMBEDDEDPORT;
				(stats->iidembeddedport)++;
			}
			else if(addr->ip6.s6_addr32[2] == 0 && \
			        	         ((addr->ip6.s6_addr16[7] & htons(0xff00)) == 0 && is_service_port(ntohs(addr->ip6.s6_addr16[6])))){
				addr->iidtype= IID_EMBEDDEDPORTREV;
				(stats->iidembeddedportrev)++;
			}
			else if(addr->ip6.s6_addr32[2] == 0 && (addr->ip6.s6_addr16[6] & htons(0xff00)) == 0 && addr->ip6.s6_addr16[7] != 0){
				addr->iidtype= IID_LOWBYTE;
				(stats->iidlowbyte)++;
			}
			else if( ntohs(addr->ip6.s6_addr16[4]) <= 0x255 && ntohs(addr->ip6.s6_addr16[5]) <= 0x255 && \
					ntohs(addr->ip6.s6_addr16[6]) <= 0x255 && ntohs(addr->ip6.s6_addr16[7]) <= 0x255){
				addr->iidtype= IID_EMBEDDEDIPV4_64;
				(stats->iidembeddedipv4_64)++;
			}
			else if( zero_byte_iid(&(addr->ip6)) > 2 ){
				addr->iidtype= IID_PATTERN_BYTES;
				(stats->iidpatternbytes)++;
			}
			else{
				addr->iidtype= IID_RANDOM;
				(stats->iidrandom)++;
			}
		}
	}
}



/*
 * Function: print_dec_address_script()
 *
 * Print the IPv6 address decode obtained by decode_ipv6_address
 */

void print_dec_address_script(struct decode6 *addr){
	unsigned int r;

	char *nullstring="";
	char *unspecified="unspecified";
	char iidsubtypebuffer[9];
	char *ipv6unspec="unspecified";
	char *ipv6multicast="multicast";
	char *ipv6unicast="unicast";

	char *ucastloopback="loopback";
	char *ucastv4mapped="ipv4-mapped";
	char *ucastv4compat="ipv4-compatible";
	char *ucastlinklocal="link-local";
	char *ucastsitelocal="site-local";
	char *ucastuniquelocal="unique-local";
	char *ucast6to4="6to4";
	char *ucastteredo="teredo";
	char *ucastglobal="global";

	char *mcastpermanent="permanent";
	char *mcastnonpermanent="non-permanent";
	char *mcastinvalid="invalid";
	char *mcastunicastbased="unicast-based";
	char *mcastembedrp="embedded-rp";
	char *mcastunknown="unknown";

	char *iidmacderived="ieee-derived";
	char *iidisatap="isatap";
	char *iidmbeddedipv4="embedded-ipv4";
	char *iidembeddedport="embedded-port";
	char *iidembeddedportrev="embedded-port-rev";
	char *iidlowbyte="low-byte";
	char *iidembeddedipv4_64="embedded-ipv4-64";
	char *iidpatternbytes="pattern-bytes";
	char *iidrandom="randomized";

	char *scopereserved="reserved";
	char *scopeinterface="interface";
	char *scopelink="link";
	char *scopeadmin="admin";
	char *scopesite="site";
	char *scopeorganization="organization";
	char *scopeglobal="global";
	char *scopeunassigned="unassigned";
	char *scopeunspecified="unspecified";

	char *type, *subtype, *scope, *iidtype, *iidsubtype;

	iidtype= nullstring;
	iidsubtype= nullstring;

	switch(addr->type){
		case IPV6_UNSPEC:
			type= ipv6unspec;
			subtype= unspecified;
			iidtype= unspecified;
			iidsubtype=unspecified;
			break;

		case IPV6_UNICAST:
			type= ipv6unicast;
			iidtype= unspecified;
			iidsubtype=unspecified;

			switch(addr->subtype){
				case UCAST_LOOPBACK:
					subtype= ucastloopback;
					iidtype= iidlowbyte;
					break;

				case UCAST_GLOBAL:
					subtype= ucastglobal;
					break;

				case UCAST_V4MAPPED:
					subtype= ucastv4mapped;
					break;

				case UCAST_V4COMPAT:
					subtype= ucastv4compat;
					break;

				case UCAST_LINKLOCAL:
					subtype= ucastlinklocal;
					break;

				case UCAST_SITELOCAL:
					subtype= ucastsitelocal;
					break;

				case UCAST_UNIQUELOCAL:
					subtype= ucastuniquelocal;
					break;

				case UCAST_6TO4:
					subtype= ucast6to4;
					break;

				case UCAST_TEREDO:
					subtype= ucastteredo;
					break;
			}


			if(addr->subtype==UCAST_GLOBAL || addr->subtype==UCAST_V4MAPPED || addr->subtype==UCAST_V4COMPAT || \
				addr->subtype==UCAST_LINKLOCAL || addr->subtype==UCAST_SITELOCAL || addr->subtype==UCAST_UNIQUELOCAL ||\
				addr->subtype == UCAST_6TO4){

				switch(addr->iidtype){
					case IID_MACDERIVED:
						iidtype= iidmacderived;
						iidsubtype= iidsubtypebuffer;

						r=snprintf(iidsubtypebuffer, sizeof(iidsubtypebuffer), "%02x-%02x-%02x", (addr->iidsubtype >> 16 & 0xff), 
									(addr->iidsubtype >> 8 & 0xff), (addr->iidsubtype & 0xff));

						if(r == 8)
							iidsubtype= iidsubtypebuffer;

						break;

					case IID_ISATAP:
						iidtype= iidisatap;
						break;

					case IID_EMBEDDEDIPV4:
						iidtype= iidmbeddedipv4;
						break;

					case IID_EMBEDDEDPORT:
						iidtype= iidembeddedport;
						break;

					case IID_EMBEDDEDPORTREV:
						iidtype= iidembeddedportrev;
						break;

					case IID_LOWBYTE:
						iidtype= iidlowbyte;
						break;

					case IID_EMBEDDEDIPV4_64:
						iidtype= iidembeddedipv4_64;
						break;

					case IID_PATTERN_BYTES:
						iidtype= iidpatternbytes;
						break;

					case IID_RANDOM:
						iidtype= iidrandom;
						break;
				}
			}

			break;

		case IPV6_MULTICAST:
			type= ipv6multicast;
			iidtype= unspecified;
			iidsubtype= unspecified;

			switch(addr->subtype){
				case MCAST_PERMANENT:
					subtype= mcastpermanent;
					break;

				case MCAST_NONPERMANENT:
					subtype= mcastnonpermanent;
					break;

				case MCAST_INVALID:
					subtype= mcastinvalid;
					break;

				case MCAST_UNICASTBASED:
					subtype= mcastunicastbased;
					break;

				case MCAST_EMBEDRP:
					subtype= mcastembedrp;
					break;

				case MCAST_UNKNOWN:
					subtype= mcastunknown;
					break;
			}
	}


	switch(addr->scope){
		case SCOPE_RESERVED:
			scope= scopereserved;
			break;

		case SCOPE_INTERFACE:
			scope= scopeinterface;
			break;

		case SCOPE_LINK:
			scope= scopelink;
			break;

		 case SCOPE_ADMIN:
			scope= scopeadmin;
			break;

		case SCOPE_SITE:
			scope= scopesite;
			break;

		case SCOPE_ORGANIZATION:
			scope= scopeorganization;
			break;

		case SCOPE_GLOBAL:
			scope= scopeglobal;
			break;

		case SCOPE_UNSPECIFIED:
			scope= scopeunspecified;
			break;

		default:
			scope= scopeunassigned;
			break;
	}

	printf("%s:%s:%s:%s:%s\n", type, subtype, scope, iidtype, iidsubtype);
}


/*
 * Function: usage()
 *
 * Prints the syntax of the scan6 tool
 */

void usage(void){
	puts("usage: address6 -i INTERFACE (-L | -d) [-s SRC_ADDR[/LEN] | -f] \n"
	     "       [-S LINK_SRC_ADDR | -F] [-p PROBE_TYPE] [-Z PAYLOAD_SIZE] [-o SRC_PORT]\n"
	     "       [-Q IPV4_PREFIX[/LEN]] [-T] [-I INC_SIZE] [-r RATE(bps|pps)] [-l]\n"
	     "       [-z SECONDS] [-c CONFIG_FILE] [-v] [-h]");
}


/*
 * Function: print_help()
 *
 * Prints help information for the scan6 tool
 */

void print_help(void){
	puts(SI6_TOOLKIT);
	puts( "address6: An IPv6 address analysis tool\n");
	usage();
    
	puts("\nOPTIONS:\n"
	     "  --stdin, -s                 Read IPv6 addresses from stdin (standard input)\n"
	     "  --decode, -d                Decode IPv6 addresses\n"
	     "  --help, -h                  Print help for the address6 tool\n"
	     "  --verbose, -v               Be verbose\n"
	     "\n"
	     " Programmed by Fernando Gont for SI6 Networks <http://www.si6networks.com>\n"
	     " Please send any bug reports to <fgont@si6networks.com>\n"
	);
}


/*
 * Function: Strnlen()
 *
 * Our own version of strnlen(), since some OSes do not support it.
 */

size_t Strnlen(const char *s, size_t maxlen){
	size_t i=0;

	while(s[i] != 0 && i < maxlen)
		i++;

	if(i < maxlen)
		return(i);
	else
		return(maxlen);
}


/*
 * Function: init_host_list()
 *
 * Initilizes a host_list structure
 */

int init_host_list(struct host_list *hlist){
	unsigned int i;

	bzero(hlist, sizeof(struct host_list));

	if( (hlist->host = malloc(MAX_LIST_ENTRIES * sizeof(struct host_entry *))) == NULL){
		return(0);
	}

	for(i=0; i < MAX_LIST_ENTRIES; i++)
		hlist->host[i]= NULL;

	hlist->nhosts= 0;
	hlist->maxhosts= MAX_HOST_ENTRIES;
	hlist->key_l= rand();
	hlist->key_h= rand();
	return(1);
}


/*
 * Function: key()
 *
 * Compute a key for accessing the hash-table of a host_list structure
 */

u_int16_t key(struct host_list *hlist, struct in6_addr *ipv6){
		return( ((hlist->key_l ^ ipv6->s6_addr16[0] ^ ipv6->s6_addr16[7]) \
				^ (hlist->key_h ^ ipv6->s6_addr16[1] ^ ipv6->s6_addr16[6])) % MAX_LIST_ENTRIES);
}


/*
 * Function: add_host_entry()
 *
 * Add a host_entry structure to the hash table
 */

struct host_entry *add_host_entry(struct host_list *hlist, struct in6_addr *ipv6){
	struct host_entry	*hentry, *ptr;
	u_int16_t			hkey;

	hkey= key(hlist, ipv6);

	if(hlist->nhosts >= hlist->maxhosts){
		return(NULL);
	}

	if( (hentry= malloc(sizeof(struct host_entry))) == NULL){
		return(NULL);
	}

	bzero(hentry, sizeof(struct host_entry));
	hentry->ip6 = *ipv6;
	hentry->next= NULL;

	if(hlist->host[hkey] == NULL){
		/* First node in chain */
		hlist->host[hkey]= hentry;
		hentry->prev= NULL;
	}
	else{
		/* Find last node in list */
		for(ptr=hlist->host[hkey]; ptr->next != NULL; ptr= ptr->next);

		hentry->prev= ptr;
		ptr->next= hentry;
	}

	(hlist->nhosts)++;

	return(hentry);
}


/*
 * Function: is_ip6_in_list()
 *
 * Checks whether an IPv6 address is present in a host list.
 */

unsigned int is_ip6_in_list(struct host_list *hlist, struct in6_addr *target){
	u_int16_t			ckey;
	struct host_entry	*chentry;

	ckey= key(hlist, target);

	for(chentry= hlist->host[ckey]; chentry != NULL; chentry=chentry->next)
		if( is_eq_in6_addr(target, &(chentry->ip6)) )
			return 1;

	return 0; 
}


/*
 * Function: is_eq_in6_addr()
 *
 * Compares two IPv6 addresses. Returns 1 if they are equal.
 */

int is_eq_in6_addr(struct in6_addr *ip1, struct in6_addr *ip2){
	unsigned int i;

	for(i=0; i<8; i++)
		if(ip1->s6_addr16[i] != ip2->s6_addr16[i])
			return 0;

	return 1;
}



/*
 * Function: print_stats()
 *
 * Prints IPv6 address statistics
 */

void print_stats(struct stats6 *stats){
	unsigned int	totaliids=0;
	puts("\n** IPv6 General Address Analysis **\n");
	printf("Total IPv6 addresses: %u\n", stats->total);
	printf("Unicast: %7u (%.2f%%)\t\tMulticast: %7u (%.2f%%)\n", stats->ipv6unicast, \
			(float)(stats->ipv6unicast)/stats->total, stats->ipv6multicast, (float)(stats->ipv6multicast)/stats->total);
	printf("Unspec.: %7u (%.2f%%)\n\n", stats->ipv6unspecified, (float)(stats->ipv6unspecified)/stats->total);

	if(stats->ipv6unicast){
		puts("** IPv6 Unicast Addresses **\n");
		printf("Loopback:     %7u (%.2f%%)\t\tIPv4-mapped:  %7u (%.2f%%)\n",\
				stats->ucastloopback, ((float)(stats->ucastloopback)/stats->ipv6unicast) * 100, stats->ucastv4mapped, \
				((float)(stats->ucastv4mapped)/stats->ipv6unicast) * 100);

		printf("IPv4-compat.: %7u (%.2f%%)\t\tLink-local:   %7u (%.2f%%)\n", stats->ucastv4compat, \
				((float)(stats->ucastv4compat)/stats->ipv6unicast) * 100, stats->ucastlinklocal, \
				((float)(stats->ucastlinklocal)/stats->ipv6unicast) * 100);

		printf("Site-local:   %7u (%.2f%%)\t\tUnique-local: %7u (%.2f%%)\n", stats->ucastsitelocal, \
				((float)(stats->ucastsitelocal)/stats->ipv6unicast) * 100, stats->ucastuniquelocal, \
				((float)(stats->ucastuniquelocal)/stats->ipv6unicast) * 100);

		printf("6to4:         %7u (%.2f%%)\t\tTeredo:       %7u (%.2f%%)\n", stats->ucast6to4, \
				((float)(stats->ucast6to4)/stats->ipv6unicast) * 100, stats->ucastteredo, \
				((float)(stats->ucastteredo)/stats->ipv6unicast) * 100);

		printf("Global:       %7u (%.2f%%)\n\n", stats->ucastglobal, ((float)(stats->ucastglobal)/stats->ipv6unicast) * 100);
	}

	if(stats->ipv6multicast){
		puts("** IPv6 Multicast Addresses **\n");
		puts("+ Multicast Address Types +");
		printf("Permanent:   %7u (%.2f%%)\t\tNon-permanent  %7u (%.2f%%)\n",\
				stats->mcastpermanent, ((float)(stats->mcastpermanent)/stats->ipv6multicast) * 100, stats->mcastnonpermanent, \
				((float)(stats->mcastnonpermanent)/stats->ipv6multicast) * 100);

		printf("Invalid:     %7u (%.2f%%)\t\tUnicast-based: %7u (%.2f%%)\n", stats->mcastinvalid, \
				((float)(stats->mcastinvalid)/stats->ipv6multicast) * 100, stats->mcastunicastbased, \
				((float)(stats->mcastunicastbased)/stats->ipv6multicast) * 100);

		printf("Embedded-RP: %7u (%.2f%%)\t\tUnknown:       %7u (%.2f%%)\n\n", stats->mcastembedrp, \
				((float)(stats->mcastembedrp)/stats->ipv6multicast) * 100, stats->mcastunknown, \
				((float)(stats->mcastunknown)/stats->ipv6multicast) * 100);

		puts("+ Multicast Address Scopes +");

		printf("Reserved:    %7u (%.2f%%)\t\tInterface.:    %7u (%.2f%%)\n",\
				stats->mscopereserved, ((float)(stats->mscopereserved)/stats->ipv6multicast) * 100, stats->mscopeinterface, \
				((float)(stats->mscopeinterface)/stats->ipv6multicast) * 100);

		printf("Link:        %7u (%.2f%%)\t\tAdmin:         %7u (%.2f%%)\n", stats->mnscopelink, \
				((float)(stats->mnscopelink)/stats->ipv6multicast) * 100, stats->mscopeadmin, \
				((float)(stats->mscopeadmin)/stats->ipv6multicast) * 100);

		printf("Site:        %7u (%.2f%%)\t\tOrganization:  %7u (%.2f%%)\n", stats->mscopesite, \
				((float)(stats->mscopesite)/stats->ipv6multicast) * 100, stats->mscopeorganization, \
				((float)(stats->mscopeorganization)/stats->ipv6multicast) * 100);

		printf("Global:      %7u (%.2f%%)\t\tUnassigned:    %7u (%.2f%%)\n\n", stats->mscopeadmin, \
				((float)(stats->mscopeadmin)/stats->ipv6multicast) * 100, stats->mscopesite, \
				((float)(stats->mscopesite)/stats->ipv6multicast) * 100);
	}

	totaliids= stats->ucastglobal + stats->ucastv4mapped + stats->ucastv4compat + stats->ucastlinklocal + \
				stats->ucastsitelocal + stats->ucastuniquelocal + stats->ucast6to4;

	if(totaliids){
		puts("** IPv6 Interface IDs **\n");

		printf("Total IIDs analyzed: %u\n", totaliids);
		printf("IEEE-based: %7u (%.2f%%)\t\tISATAP:          %7u (%.2f%%)\n",\
				stats->iidmacderived, ((float)(stats->iidmacderived)/totaliids) * 100, stats->iidisatap, \
				((float)(stats->iidisatap)/totaliids) * 100);

		printf("Embed-IPv4: %7u (%.2f%%)\t\tEmbed-IPv4 (64): %7u (%.2f%%)\n", stats->iidmbeddedipv4, \
				((float)(stats->iidmbeddedipv4)/totaliids) * 100, stats->iidembeddedipv4_64, \
				((float)(stats->iidembeddedipv4_64)/totaliids) * 100);

		printf("Embed-port: %7u (%.2f%%)\t\tEmbed-port (r):  %7u (%.2f%%)\n", stats->iidembeddedport, \
				((float)(stats->iidembeddedport)/totaliids) * 100, stats->iidembeddedportrev, \
				((float)(stats->iidembeddedportrev)/totaliids) * 100);


		printf("Low-byte:   %7u (%.2f%%)\t\tByte-pattern:    %7u (%.2f%%)\n",\
				stats->iidlowbyte, ((float)(stats->iidlowbyte)/totaliids) * 100, stats->iidpatternbytes, \
				((float)(stats->iidpatternbytes)/totaliids) * 100);

		printf("Randomized: %7u (%.2f%%)\n\n", stats->iidrandom, ((float)(stats->iidrandom)/totaliids) * 100);
	}
}

