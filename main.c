#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <ws2tcpip.h>
#include<stdio.h>
#include<stdlib.h>
#include<winsock.h>
#include<stdbool.h>
#include "Winsock.h"
#pragma comment(lib,"ws2_32.lib")

//�׽��ֺ͵�ַ
SOCKET sock;
SOCKADDR_IN ser_addr, nser_addr;

//Ĭ���ⲿ�������ͱ��ػ����ļ�
char nextServer[32] = "114.114.114.114";
char file_name[32] = "dnsrelay.txt";
int local_size;		//�ļ���С
int debug_level;	//���Եȼ�

//����������Ϣ���ʶ
typedef struct req_inform
{
	SOCKADDR_IN client_addr;
	unsigned short id;
	//��ַ��id��ʶΨһDNS����
}Req_inform;
int idThen = 0;
const int idThen_max = 500;
Req_inform cache[500];

//����DNSӳ���
typedef struct local_dns
{
	char url[128];
	char ip[128];
}LocalDNS;
LocalDNS localdns[1024];

//���ⲿ�������Ļ���
typedef struct cache_table {
	char url[128];
	char ip[32];
	int ttl;
}CacheTable;
CacheTable cachetable[64];

//�����б�
void pro_para(int argc, char* argv[]);	//���������в���
void init();							//��ʼ�����򣬴����ļ���Ϣ
int init_socket();						//��ʼ��socket
int is_query(char* buffer);				//�ж��Ƿ�Ϊ��ѯ��
void deal_query(char* buffer, struct sockaddr_in req_addr, int buffer_size);	//�����ѯ��
void deal_ans(char* buffer, int buffer_size);				//����ӷ������ķ��ذ�
int is_local(char* url, int* a);		//url�Ƿ���ڱ����ļ�
int is_cache(char* url);				//url�Ƿ���ڻ�����
void create_respose(char* buffer, struct sockaddr_in request_address, int buffer_size, char ip[][32], int ipNum);	//�ڱ��ػ򻺴����ҵ�ip��ַ�����������
void ask_next_server(char* buffer, struct sockaddr_in req_addr, int buffer_size);		//�����ϼ�����������
void addCache(char* url, char* ip, int ttl);	//��ӻ���
void deal_ans(char* buffer, int buffer_size);	//����ӷ��������ص���Ϣ
char* get_url(char* buffer);			//�Ӱ�����ȡ������Ϣ
int is_expired(int expire_time);		//�ж��Ƿ�ʱ

int main(int argc, char* argv[])
{
	pro_para(argc, argv);
	char rec_buffer[512];
	SOCKADDR_IN rec_addr;
	int rec_size;

	//���뱾�ػ����ļ�
	init();

	//��ʼ��socket
	if (init_socket() == -1)
		return 0;
	printf("��ʼ��socket�ɹ�");

	while(true)
	{
		int rec_len = sizeof(struct sockaddr);
		if ((rec_size = recvfrom(sock, rec_buffer, sizeof(rec_buffer), 0,
			(struct sockaddr*) & rec_addr, &rec_len)) == -1)
		{
			printf("���հ�ʱ��������!\n");
			continue;
		}
		else if (is_query(rec_buffer))
		{
			//���������
			deal_query(rec_buffer, rec_addr, rec_size);
		}
		else
		{
			//�����ذ�
			deal_ans(rec_buffer, rec_size);
		}
	}
}

void init()
{
	FILE* fp = NULL;
	fp = fopen(file_name, "r");
	if (fp == NULL)
	{
		printf("�ļ���ʧ��\n");
		return;
	}
	int count = 0;
	while (!feof(fp))
	{
		int i = 0;
		int j = 0;
		char temp[250];
		char url[100];
		char ip[100];
		fgets(temp, 250, fp);
		
		for (i; temp[i] != ' '; i++)
		{
			ip[i] = temp[i];
		}
		ip[i] = '\0';
		i++;
		for (j = 0,i; temp[i] != '\0' && temp[i] != '\n'; j++,i++)
		{
			url[j] = temp[i];
		}
		url[j] = '\0';
		
		strcpy(localdns[count].url, url);
		strcpy(localdns[count].ip, ip);
		count += 1;

	}
	
	local_size = count;
	if(debug_level == 2)
		printf("��%d�������¼\n", count);
}

/*
	@para:
		buffer:�����
	@return
		url
*/
char* get_url(char* buffer)
{
	//��ȡURL �ӱ��ĵĵ�12���ֽڿ�ʼ
	char* buf = buffer + 12;
	int len = buf[0];

	int length = 0;
	int index = 0;
	int url_index = 0;
	int url_size = 0;
	length = buf[index++];
	while (length)
	{
		url_size += length + 1;
		for (int i = 0; i < length; i++)
		{
			index++;
		}
		length = buf[index++];
	}
	index = 0;
	char* url = malloc((url_size - 1) * sizeof(char));
	length = buf[index++];

	while (length)
	{
		for (int i = 0; i < length; i++)
		{
			url[url_index++] = buf[index++];
		}
		length = buf[index++];
		if (length != 0)
			url[url_index++] = '.';
	}
	url[url_size - 1] = '\0';
	return url;
}

int is_expired(int expire_time)
{
	if (expire_time == 0)
		return 0;
	time_t now_time;
	now_time = time(NULL);
	if (now_time > expire_time)
		return 1;
	return 0;
}

//��ʼ��socket
int init_socket()
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		WSACleanup();
		printf("winsocket��ʼ��ʧ��");
		return -1;
	}

	//����
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	ser_addr.sin_family = AF_INET;
	ser_addr.sin_port = htons(53);
	ser_addr.sin_addr.s_addr = INADDR_ANY;

	//�ⲿ������
	nser_addr.sin_family = AF_INET;
	nser_addr.sin_port = htons(53);
	nser_addr.sin_addr.s_addr = inet_addr(nextServer);

	//�󶨶˿�
	if (bind(sock, (SOCKADDR*)& ser_addr, sizeof(ser_addr)) == -1)
	{
		printf("�󶨶˿�ʧ��\n");
		return -1;
	}
	else
		printf("�󶨶˿ڳɹ�\n");
	return 0;
}

//�Ƿ�Ϊ��ѯ��
int is_query(char* buffer)
{
	//��ѯflag�ֽ��Ƿ�Ϊ0x0001
	//	0    1    2    3    4    5    6    7    0    1    2    3    4    5    6    7
	//	+ -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- +
	//	| QR |        Opcode     | AA | TC | RD | RA |   (zero)     |     rcode         |
	//	+ -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- +
	// ��Ϊ��ѯ�������RD=1������Ϊ0��FLAG�ֶ�Ϊ0x0100
	
	unsigned short flag = 0;
	memcpy(&flag, buffer + 2, 2);
	if (flag == 0x0001)
		return 1;
	else
		return 0;
}

//�Ƿ��ڱ��ػ���
/*
	@para:
		url:��ѯ������
		a:�洢��Ӧip��ַ��localdns�е��±�
	@return
		������Ӧ��ip����
*/
int is_local(char* url, int* a)
{
	int count = 0;
	for (int i = 0; i < local_size; i++)
	{
		if (strcmp(url, localdns[i].url) == 0)
		{
			a[count] = i;
			count++;
			
		}
	}
	return count;
}

//�Ƿ�����ʱ����
int is_cache(char* url,int* a)
{
	int count = 0;
	if(debug_level == 2)
		printf("����ʱ������Ѱ��\n");
	for (int i = 0; i < 64; i++)
	{
		if (strcmp(url, cachetable[i].url) == 0)
		{
			if (is_expired(cachetable[i].ttl))
			{
				printf("��¼��ʱ\n");
				continue;
			}
			a[count] = i;
			count++;
		}
	}
	printf("count:%d\n",count);
	return count;
}

/*
	@para
		buffer:����İ�
		request_address:�����ַ
		buffer_size:������Ĵ�С
		ip:�����ip,��һ����ά����
		ipNum:�𰸸���
*/
void create_respose(char* buffer, struct sockaddr_in request_address, int buffer_size, char ip[][32],int ipNum)
{
	char answer_packet[1024];	//��Ӧ��
	//	0    1    2    3    4    5    6    7    0    1    2    3    4    5    6    7
	//	+ -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- +
	//	| QR |        Opcode     | AA | TC | RD | RA |   (zero)     |     rcode         |
	//	+ -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- +
	unsigned short flags = 0x8081;	//��Ӧ����־λ��8081=1000 0000 1000 0001������˳�򣩣�����˳��Ϊ1000 0001 1000 0000����ʾ���ݰ�Ϊ��Ӧ���������ݹ飬���õݹ飬û�в��
	unsigned short answer_number = htons(ipNum);	//��������Ӧ��������Ӧ����1��һ������Ĭ��һ��������Ӧһ��ip��ַ������������Ӧ����Ϊ1 ����˳��Ϊ0x0001
	if(debug_level == 2)
		printf("���ڱ����������������ҵ���");
	memcpy(answer_packet, buffer, buffer_size);	//��ʱ��buffer���ǿͻ��˷������������
	memcpy(answer_packet + 2, &flags, 2);		//�ı�־λ��IDֵ���ñ䣨ռ�����ֽڣ�����־λ��IDֵ�ĺ��棨ռ�����ֽڣ�
	if (strcmp(ip, "0.0.0.0") == 0)				//����Ҫ�������õ�ip��ַΪ0.0.0.0�����ʾ����������,������
	{
		answer_number = 0x0000;	//���ûظ���Ϊ0
		memcpy(&answer_packet[6], &answer_number, sizeof(unsigned short));	//�ӵ������ֽڿ�ʼ��Answers RR���ش� ��Դ��¼����
		if (sendto(sock, answer_packet, buffer_size, 0, (struct sockaddr*) & request_address, sizeof(struct sockaddr)) == -1)
		{
			printf("���ع���δ�ܳɹ��������ͻ��ˣ�\n");
			return;
		}
		return;
	}
	else
	{
		//�������Ӧ��
		memcpy(answer_packet + 6, &answer_number, sizeof(unsigned short));	//�޸ġ������Ϣ��������Ӧ�����������Ӧ�ֶε�
		//��Դ��¼��ǰ������������ֱ��ʹ�ÿͻ��˷�����������е����ݼ��ɣ�����Ҫ�����޸�

		//����DNS�ش����򲿷�
		int curLen = 0;					//�洢����Ӧ���ֵĳ���
		char answer[256];				//�𰸰�����
		//���ڿ��ܴ����һ��������Ӧ���IP��ַ�����
		for (int i = 0; i < ipNum; i++)
		{
			//Name��������
			//c00c=1100 0000 0000 1100����ߵ���λΪ11������ʶ��ָ�룬�����14λ��DNS���ĵĿ�ʼ��������ָ���ñ����е���Ӧ�ֽ���
			unsigned short Name = htons(0xc00c);	//12������DNS����Э��ͷ���ĳ��ȣ�֮����ǲ�ѯ�������򣬼���Ҫ���в�ѯ������
			//htons���������ͱ����������ֽ�˳��ת��Ϊ�����ֽ�˳��
			memcpy(answer+curLen, &Name, sizeof(unsigned short));
			curLen += sizeof(unsigned short);

			//��ѯ����Type
			unsigned short TypeA = htons(0x0001);//����������A�����������IPv4��ַ
			memcpy(answer + curLen, &TypeA, sizeof(unsigned short));
			curLen += sizeof(unsigned short);

			//��ѯ��Class
			unsigned short ClassA = htons(0x0001);//��ѯ��ͨ��Ϊ1��������Internet����
			memcpy(answer + curLen, &ClassA, sizeof(unsigned short));
			curLen += sizeof(unsigned short);

			//����ʱ��
			unsigned long timeLive = htons(0x7b);
			memcpy(answer + curLen, &timeLive, sizeof(unsigned long));
			curLen += sizeof(unsigned long);

			//��Դ���ݳ���
			//IP��Դ���ݳ���Ϊ4���ֽ�
			char copy_ip[64];
			strcpy(copy_ip, ip[i]);
			copy_ip[strlen(ip)] = '\0';
			unsigned short IPLen = htons(0x0004);
			memcpy(answer + curLen, &IPLen, sizeof(unsigned short));
			curLen += sizeof(unsigned short);

			//Data��Դ����
			unsigned long IP = (unsigned long)inet_addr(copy_ip);
			memcpy(answer + curLen, &IP, sizeof(unsigned long));
			curLen += sizeof(unsigned long);
		}
	
		//����֮ǰ��ѯ���ĵĳ���
		curLen += buffer_size;
		//�����ĺ���Ӧ���ֹ�ͬ���DNS��Ӧ���Ĵ���sendbuf
		memcpy(answer_packet + buffer_size, answer, curLen);

		if (sendto(sock, answer_packet, curLen, 0, (struct sockaddr*) & request_address, sizeof(struct sockaddr)) == -1)
		{
			printf("�����δ�ܳɹ��������ͻ���!\n");
			return;
		}
	}

}

//�����ϼ�������
/*
	@para
		buffer:����İ�
		request_address:�����ַ
		buffer_size:������Ĵ�С
*/
void ask_next_server(char* buffer, struct sockaddr_in req_addr, int buffer_size)
{
	Req_inform tem;
	if(debug_level == 2)
		printf("��������������δ�ҵ���ѯ���ⲿ������\n");
	tem.client_addr = req_addr;	//����˵�ַ
	memcpy(&(tem.id), buffer, 2);	//ԭ�����id
	memcpy(buffer, &idThen, 2);	//�����µ�id��idThen
	if (sendto(sock, buffer, buffer_size, 0, (struct sockaddr*) & nser_addr, sizeof(struct sockaddr)) == -1)
	{
		printf("δ�����ɹ����ⲿDNS������\n");
		return;
	}
	cache[idThen] = tem;	//���ӱ���idת������ӳ�����
	idThen = (idThen + 1) % idThen_max;
}

/*
	@para
		buffer:����İ�
		request_address:�����ַ
		buffer_size:������Ĵ�С
*/
void deal_query(char* buffer, struct sockaddr_in req_addr, int buffer_size)
{
	if(debug_level == 2)
		printf("����������е�url��Ϣ\n");

	//��ȡURL �ӱ��ĵĵ�12���ֽڿ�ʼ
	//�Ӱ�����ȡ��������ʽΪ3www5baidu3com0
	char* url = get_url(buffer);

	unsigned short type = 0;
	memcpy(&type, buffer + buffer_size - 4, 2);
	int isIpv6 = 0;
	if (type == 0x0100)
	{
		if(debug_level==2)
			printf("IPV4��ѯ��\n");
	}
	else if (type == 0x1c00)
	{
		if(debug_level == 2)
			printf("IPV6��ѯ��\n");
		isIpv6 = 1;
	}
		
	else
		printf("δ֪��ѯ��:%d\n",type);

	//������������ӦIP�ĸ���
	int ipNum = 0;//����ip������
	int ipNum2 = 0;//����ip������
	//��ʼ���±�
	int ip_loc[5] = { -1,-1,-1,-1,-1 };
	int ip_loc2[5] = { -1,-1,-1,-1,-1 };
	//�洢�ҵ���ip��Ϣ
	char ipValue[5][32];
	char ipValue2[5][32];
	//�Ƿ��ڱ����ļ���
	ipNum = is_local(url, ip_loc);
	ipNum2 = is_cache(url, ip_loc2);
	//�Ƿ��ڻ�����
	for(int i = 0; i < ipNum; i++)
	{
		strcpy(ipValue[i], localdns[ip_loc[i]].ip);
	}
	for (int i = 0; i < ipNum2; i++)
	{
		strcpy(ipValue2[i],cachetable[ip_loc2[i]].ip);
	}
	if (ipNum)
	{//���url�ڱ��ػ���
		printf("�ڱ��ػ����в��ҵ�%d����Ϣ\n", ipNum);
		for (int i = 0; i < ipNum; i++)
			printf("%s\n", ipValue[i]);
		create_respose(buffer, req_addr, buffer_size, ipValue,ipNum);
	}
	else if (ipNum2)
	{
		printf("����ʱ�����в��ҵ�%d����Ϣ\n",ipNum2);
		for (int i = 0; i < ipNum2; i++)
			printf("%s\n", ipValue2[i]);
		create_respose(buffer, req_addr, buffer_size, ipValue2,ipNum2);
	}
	else
	{//������ڣ������ϼ�����������
		printf("�����ϼ�������...\n");
		ask_next_server(buffer, req_addr, buffer_size);
	}
}

/*
	@para
		url:���������
		ip:���ⲿ�������õ���ip 
		ttl������ʱ��
*/
void addCache(char* url, char* ip, int ttl)
{
	int minTime = time(NULL)+ttl;
	int pos = -1;
	int url_pos = -1;
	int time_pos = -1;
	//�ҳ����п���λ�úͳ�ʱλ�ã����������ʱλ�õĻ��档�����߶��ޣ� ���ҳ����ȳ�ʱ��λ��
	for (int i = 0; i < 64; i++)
	{
		if (strcmp("", cachetable[i].url) == 0)
		{
			if(debug_level == 2)
				printf("λ��%d����\n", i);
			if(url_pos < 0)
				url_pos = i;
		}
		else if (is_expired(cachetable[i].ttl))
		{
			if(debug_level == 2)
				printf("λ��%d��ʱ\n", i);
			if (pos < 0)
			{
				pos = i;
				break;
			}
				
		}
		else
		{
			if (cachetable[i].ttl <= minTime)
			{
				time_pos = i;
				minTime = cachetable[i].ttl;
			}
				
		}
	}

	if (pos < 0)
	{
		if (url_pos >= 0)
		{
			pos = url_pos;
		}
			
		else
			pos = time_pos;
	}
	//���»���
	if(debug_level == 2)
		printf("����д���λ��:%d", pos);
	strcpy(cachetable[pos].url, url);
	strcpy(cachetable[pos].ip, ip);
	time_t nowTime;
	nowTime = time(NULL);
	cachetable[pos].ttl = nowTime + ttl;
}

void deal_ans(char* buffer, int buffer_size)
{
	unsigned short cur_id, i;
	//map<unsigned short, Req_inform>::iterator miter;
	Req_inform cur_inform;
	if(debug_level == 2)
		printf("���ⲿ�������õ���Ӧ��\n");
	memcpy(&cur_id, buffer, 2);

	//��ID��������ҵ�
	cur_inform = cache[cur_id];
	memcpy(buffer, &(cur_inform.id), 2);

	char* url = NULL;
	//��ȡ��������ͻش����
	int queryNum = ntohs(*((unsigned short*)(buffer + 4)));
	int answerNum = ntohs(*((unsigned short*)(buffer + 6)));
	char* p = buffer + 12;

	if(debug_level == 2)
		printf("�������: %d, �ش����: %d\n", queryNum, answerNum);
	for (int i = 0; i < queryNum; i++)
	{
		url = get_url(buffer);
		while (*p > 0)  //��ȡ��ʶ��ǰ�ļ����������url
			p += (*p) + 1;
		p += 5; //����00��type������Ϣ
	}
	//�����ظ�
	//����ο�DNS�ظ����ĸ�ʽ
	int ip1, ip2, ip3, ip4;
	char ip[32];
	for (int i = 0; i < answerNum; ++i)
	{
		if ((unsigned char)* p == 0xc0) //��ָ�������
			p += 2;
		else
		{
			//���ݼ�������url
			while (*p > 0)
				p += (*p) + 1;
			++p;    //ָ����������
		}
		unsigned short resp_type = ntohs(*(unsigned short*)p);  //�ظ�����
		p += 2;
		unsigned short resp_class = ntohs(*(unsigned short*)p); //�ظ���
		p += 2;
		unsigned short high = ntohs(*(unsigned short*)p);   //����ʱ���λ
		p += 2;
		unsigned short low = ntohs(*(unsigned short*)p);    //����ʱ���λ
		p += 2;
		int ttl = (((int)high) << 16) | low;    //�ߵ�λ��ϳ�����ʱ��
		int datalen = ntohs(*(unsigned short*)p);   //�������ݳ���
		p += 2;

		if (resp_type == 1) //IPV4����
		{
			//��ȡ4��ip����
			ip1 = (unsigned char)* p++;
			ip2 = (unsigned char)* p++;
			ip3 = (unsigned char)* p++;
			ip4 = (unsigned char)* p++;

			sprintf(ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);

			// ������ⲿ�������н��ܵ���������Ӧ��IP
			addCache(url, ip, 10);
			break;
		}
		else p += datalen;  //ֱ������
	}
	if (sendto(sock, buffer, buffer_size, 0, (struct sockaddr*) & (cur_inform.client_addr), sizeof(struct sockaddr)) == -1)
	{
		printf("���Ϳͻ���ʧ��\n");
	}
	else
		printf("���Ϳͻ��˳ɹ�\n");
}

void pro_para(int argc, char* argv[])
{
	for (int i = 1; i < argc; ++i)
	{
		if (argv[i][0] == '-')
		{
			if (argv[i][1] == 'd' && argv[i][2] == 'd')
				debug_level = 2;
			else debug_level = 1;
			printf("���Եȼ�:%d\n", debug_level);
		}
		else
		{
			printf("%s\n", argv[i]);
			if (argv[i][0] >= '0' && argv[i][0] <= '9')
			{
				//�����ⲿ��������ַ
				strcpy(nextServer, argv[i]);
				printf("��һվ������:%s\n", nextServer);
			}
			else
			{
				//�����ļ���ַ
				strcpy(file_name, argv[i]);
				printf("���ػ����ļ�:%s\n", file_name);
			}
			
		}
	}
}

/*
	���θ���:
	1��is_local��is_cache�Ĳ��Һ���������һ��������������ڱ����±꣬���ص��ǲ��ҵ�������
	2��create_respose������ԭ���Ĳ���ip��һά����仯����ά���飬ipNumΪ���ҵ�������
		�ú����ṹδ����٣�����һ��������forѭ��
	3��������ͷ���������ǰ��,ɾȥ��һЩ����Ҫ�Ķ���
*/