#include <stdio.h>
#include "pcap.h"

/* 4 bytes IP address */
typedef struct ip_address
{
	u_char byte1;
	u_char byte2;
	u_char byte3;
	u_char byte4;
}ip_address;

/* 6 bytes MAC address */
typedef struct mac_address
{
	u_char byte1;
	u_char byte2;
	u_char byte3;
	u_char byte4;
	u_char byte5;
	u_char byte6;
}mac_address;

/* IPv4 header */
typedef struct ip_header
{
	u_char	ver_ihl;		// Version (4 bits) + Internet header length (4 bits)
	u_char	tos;			// Type of service 
	u_short tlen;			// Total length 
	u_short identification; // Identification
	u_short flags_fo;		// Flags (3 bits) + Fragment offset (13 bits)
	u_char	ttl;			// Time to live
	u_char	proto;			// Protocol
	u_short crc;			// Header checksum
	ip_address	saddr;		// Source address
	ip_address	daddr;		// Destination address
	u_int	op_pad;			// Option + Padding
}ip_header;

/* UDP header*/
typedef struct udp_header
{
	u_short sport;			// Source port
	u_short dport;			// Destination port
	u_short len;			// Datagram length
	u_short crc;			// Checksum
}udp_header;

/* NAT table item*/
typedef struct natitem
{
	ip_address saddr;
	u_char sport[2];
	u_char dport[2];
}natitem;

/* prototype of the function */
void packet_handler(u_char *param, const struct pcap_pkthdr *header, const u_char *pkt_data);
int sendpack(pcap_t *adhandle,unsigned char* packet,int len);
void icopy(u_char* s,u_char* d,int len);
int icomp(u_char* a,u_char* b,int len);

u_char packet[1000];
u_char thismac[6]={0xb8,0x88,0xe3,0x80,0xa6,0x77};
u_char thisip[4]={192,168,0,1};
natitem nattable[100];
int natnum;
u_char clientmac[100][6];
int clientnum;
int autoport;

int main()
{
	pcap_if_t *alldevs;
	pcap_if_t *d;
	int inum;
	int i=0;
	pcap_t *adhandle;
	char errbuf[64];//PCAP_ERRBUF_SIZE 256

	natnum=0;
	clientnum=0;
	autoport=10000;
	
	/* Retrieve the device list */
	if (pcap_findalldevs(&alldevs, errbuf) == -1)
	{
		printf("Error in pcap_findalldevs: %s\n", errbuf);
		exit(1);
	}
	
	/* Print the list */
	for(d=alldevs;d;d=d->next)
	{
		printf("%d. %s", ++i, d->name);
		if (d->description)
			printf(" (%s)\n", d->description);
		else
			printf(" (No description available)\n");
	}
	
	if(i==0)
	{
		printf("\nNo interfaces found! Make sure WinPcap is installed.\n");
		return -1;
	}
	
	printf("Enter the interface number (1-%d):",i);
	//scanf("%d", &inum);

	inum=2;
	
	if(inum < 1 || inum > i)
	{
		printf("\nInterface number out of range.\n");
		/* Free the device list */
		pcap_freealldevs(alldevs);
		return -1;
	}
	
	/* Jump to the selected adapter */
	for(d=alldevs, i=0; i< inum-1 ;d=d->next, i++);
	
	/* Open the device */
	/* Open the adapter */
	if ((adhandle= pcap_open_live(d->name,	// name of the device
							 65536,			// portion of the packet to capture. 
											// 65536 grants that the whole packet will be captured on all the MACs.
							 1,				// promiscuous mode (nonzero means promiscuous)
							 1000,			// read timeout
							 errbuf			// error buffer
							 )) == NULL)
	{
		fprintf(stderr,"\nUnable to open the adapter. %s is not supported by WinPcap\n", d->name);
		/* Free the device list */
		pcap_freealldevs(alldevs);
		return -1;
	}
	
	printf("\nlistening on %s...\n", d->description);
	
	/* At this point, we don't need any more the device list. Free it */
	pcap_freealldevs(alldevs);
	
	/* start the capture */
	pcap_loop(adhandle, 0, packet_handler, NULL);

	pcap_close(adhandle);
	return 0;
}

/* Callback function invoked by libpcap for every incoming packet */
void packet_handler(u_char *param, const struct pcap_pkthdr *header, const u_char *pkt_data)
{
	struct tm *ltime;
	char timestr[16];
	ip_header *ih;
	udp_header *uh;
	u_int ip_len;
	u_short sport,dport;
	time_t local_tv_sec;

	/*
	 * unused parameter
	 */
	(VOID)(param);

	/* convert the timestamp to readable format */
	local_tv_sec = header->ts.tv_sec;
	ltime=localtime(&local_tv_sec);
	strftime( timestr, sizeof timestr, "%H:%M:%S", ltime);

	/* print timestamp and length of the packet */
	printf("%s.%.6d len:%d ", timestr, header->ts.tv_usec, header->len);

	/* retireve the position of the ip header */
	ih = (ip_header *) (pkt_data +
		14); //length of ethernet header

	/* retireve the position of the udp header */
	ip_len = (ih->ver_ihl & 0xf) * 4;
	uh = (udp_header *) ((u_char*)ih + ip_len);

	/* convert from network byte order to host byte order */
	sport = ntohs( uh->sport );
	dport = ntohs( uh->dport );

	/* print ip addresses and udp ports */
	printf("%d.%d.%d.%d.%d -> %d.%d.%d.%d.%d\n",
		ih->saddr.byte1,
		ih->saddr.byte2,
		ih->saddr.byte3,
		ih->saddr.byte4,
		sport,
		ih->daddr.byte1,
		ih->daddr.byte2,
		ih->daddr.byte3,
		ih->daddr.byte4,
		dport);
	
	if (!icomp(thismac,(u_char *)pkt_data,6))
	{
		return 0;
	}
	if (!inClientList((u_char*)pkt_data+6))
	{
	}
	if (portInNAT())
	{
		icopy((u_char*)&(nattable[natnum].saddr),(u_char*)pkt_data+6,6);
		if (*(pkt_data+14+9)==6)
		{
			icopy((u_char*)&(nattable[natnum].sport),(u_char*)pkt_data+14+20+2,2);
		}
		if (*(pkt_data+14+9)==17)
		{
			icopy((u_char*)&(nattable[natnum].sport),(u_char*)pkt_data+14+20,2);
		}
		nattable[natnum].dport[0]=autoport/256;
		nattable[natnum].dport[1]=autoport%256;
		autoport++;
		natnum++;
		//sendpack(adhandle,packet,74);
	}
}

int portInNAT()
{
	for(int i=0;i<natnum;i++)
	{
	}
}

int inClientList(u_char* mac)
{
	for (int i=0;i<clientnum;i++)
	{
		if(icomp(clientmac[i],mac,6))
		{
			return 1;
		}
	}
	return 0;
}

int sendpack(pcap_t *adhandle,unsigned char* packet,int len)
{
	//char errbuf[PCAP_ERRBUF_SIZE];
	
	/* Check the validity of the command line 
	if (argc != 2)
	{
		printf("usage: %s interface", argv[0]);
		return 1;
	}*/
    
	/* Open the adapter 
	if ((adhandle = pcap_open_live(argv[1],		// name of the device
							 65536,			// portion of the packet to capture. It doesn't matter in this case 
							 1,				// promiscuous mode (nonzero means promiscuous)
							 1000,			// read timeout
							 errbuf			// error buffer
							 )) == NULL)
	{
		fprintf(stderr,"\nUnable to open the adapter. %s is not supported by WinPcap\n", argv[1]);
		return 2;
	}*/



	/* Send down the packet */
	if (pcap_sendpacket(adhandle,	// Adapter
		packet,				// buffer with the packet
		len					// size
		) != 0)
	{
		fprintf(stderr,"\nError sending the packet: %s\n", pcap_geterr(adhandle));
		return 3;
	}
	return 0;
}
