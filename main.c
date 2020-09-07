#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <ws2tcpip.h>
#include<stdio.h>
#include<stdlib.h>
#include<winsock.h>
#include<stdbool.h>
#include "Winsock.h"
#pragma comment(lib,"ws2_32.lib")

//套接字和地址
SOCKET sock;
SOCKADDR_IN ser_addr, nser_addr;

//默认外部服务器和本地缓存文件
char nextServer[32] = "114.114.114.114";
char file_name[32] = "dnsrelay.txt";
int local_size;		//文件大小
int debug_level;	//调试等级

//本地请求信息表标识
typedef struct req_inform
{
	SOCKADDR_IN client_addr;
	unsigned short id;
	//地址和id标识唯一DNS请求
}Req_inform;
int idThen = 0;
const int idThen_max = 500;
Req_inform cache[500];

//本地DNS映射表
typedef struct local_dns
{
	char url[128];
	char ip[128];
}LocalDNS;
LocalDNS localdns[1024];

//从外部服务器的缓存
typedef struct cache_table {
	char url[128];
	char ip[32];
	int ttl;
}CacheTable;
CacheTable cachetable[64];

//函数列表
void pro_para(int argc, char* argv[]);	//处理命令行参数
void init();							//初始化程序，处理文件信息
int init_socket();						//初始化socket
int is_query(char* buffer);				//判断是否为查询包
void deal_query(char* buffer, struct sockaddr_in req_addr, int buffer_size);	//处理查询包
void deal_ans(char* buffer, int buffer_size);				//处理从服务器的返回包
int is_local(char* url, int* a);		//url是否存在本地文件
int is_cache(char* url);				//url是否存在缓存中
void create_respose(char* buffer, struct sockaddr_in request_address, int buffer_size, char ip[][32], int ipNum);	//在本地或缓存中找到ip地址，构造包返回
void ask_next_server(char* buffer, struct sockaddr_in req_addr, int buffer_size);		//交给上级服务器处理
void addCache(char* url, char* ip, int ttl);	//添加缓存
void deal_ans(char* buffer, int buffer_size);	//处理从服务器返回的信息
char* get_url(char* buffer);			//从包里提取域名信息
int is_expired(int expire_time);		//判断是否超时

int main(int argc, char* argv[])
{
	pro_para(argc, argv);
	char rec_buffer[512];
	SOCKADDR_IN rec_addr;
	int rec_size;

	//读入本地缓存文件
	init();

	//初始化socket
	if (init_socket() == -1)
		return 0;
	printf("初始化socket成功");

	while(true)
	{
		int rec_len = sizeof(struct sockaddr);
		if ((rec_size = recvfrom(sock, rec_buffer, sizeof(rec_buffer), 0,
			(struct sockaddr*) & rec_addr, &rec_len)) == -1)
		{
			printf("接收包时发生错误!\n");
			continue;
		}
		else if (is_query(rec_buffer))
		{
			//处理请求包
			deal_query(rec_buffer, rec_addr, rec_size);
		}
		else
		{
			//处理返回包
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
		printf("文件打开失败\n");
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
		printf("共%d条缓存记录\n", count);
}

/*
	@para:
		buffer:缓存包
	@return
		url
*/
char* get_url(char* buffer)
{
	//提取URL 从报文的第12个字节开始
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

//初始化socket
int init_socket()
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		WSACleanup();
		printf("winsocket初始化失败");
		return -1;
	}

	//本地
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	ser_addr.sin_family = AF_INET;
	ser_addr.sin_port = htons(53);
	ser_addr.sin_addr.s_addr = INADDR_ANY;

	//外部服务器
	nser_addr.sin_family = AF_INET;
	nser_addr.sin_port = htons(53);
	nser_addr.sin_addr.s_addr = inet_addr(nextServer);

	//绑定端口
	if (bind(sock, (SOCKADDR*)& ser_addr, sizeof(ser_addr)) == -1)
	{
		printf("绑定端口失败\n");
		return -1;
	}
	else
		printf("绑定端口成功\n");
	return 0;
}

//是否为查询包
int is_query(char* buffer)
{
	//查询flag字节是否为0x0001
	//	0    1    2    3    4    5    6    7    0    1    2    3    4    5    6    7
	//	+ -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- +
	//	| QR |        Opcode     | AA | TC | RD | RA |   (zero)     |     rcode         |
	//	+ -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- +
	// 若为查询包，则除RD=1外其余为0。FLAG字段为0x0100
	
	unsigned short flag = 0;
	memcpy(&flag, buffer + 2, 2);
	if (flag == 0x0001)
		return 1;
	else
		return 0;
}

//是否在本地缓存
/*
	@para:
		url:查询的域名
		a:存储对应ip地址在localdns中的下标
	@return
		域名对应的ip个数
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

//是否在临时缓存
int is_cache(char* url,int* a)
{
	int count = 0;
	if(debug_level == 2)
		printf("在临时缓存中寻找\n");
	for (int i = 0; i < 64; i++)
	{
		if (strcmp(url, cachetable[i].url) == 0)
		{
			if (is_expired(cachetable[i].ttl))
			{
				printf("记录超时\n");
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
		buffer:请求的包
		request_address:请求地址
		buffer_size:请求包的大小
		ip:构造的ip,是一个二维数组
		ipNum:答案个数
*/
void create_respose(char* buffer, struct sockaddr_in request_address, int buffer_size, char ip[][32],int ipNum)
{
	char answer_packet[1024];	//响应包
	//	0    1    2    3    4    5    6    7    0    1    2    3    4    5    6    7
	//	+ -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- +
	//	| QR |        Opcode     | AA | TC | RD | RA |   (zero)     |     rcode         |
	//	+ -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- + -- +
	unsigned short flags = 0x8081;	//响应包标志位，8081=1000 0000 1000 0001（网络顺序），本地顺序为1000 0001 1000 0000，表示数据包为响应包，期望递归，可用递归，没有差错
	unsigned short answer_number = htons(ipNum);	//正常的响应数设置响应个数1，一般来讲默认一个域名对应一个ip地址，所以设置响应个数为1 本地顺序为0x0001
	if(debug_level == 2)
		printf("已在本地域名解析表中找到！");
	memcpy(answer_packet, buffer, buffer_size);	//此时的buffer就是客户端发送来的请求包
	memcpy(answer_packet + 2, &flags, 2);		//改标志位，ID值不用变（占两个字节），标志位在ID值的后面（占两个字节）
	if (strcmp(ip, "0.0.0.0") == 0)				//根据要求，如果获得的ip地址为0.0.0.0，则表示域名不存在,被屏蔽
	{
		answer_number = 0x0000;	//重置回复数为0
		memcpy(&answer_packet[6], &answer_number, sizeof(unsigned short));	//从第六个字节开始是Answers RR（回答 资源记录数）
		if (sendto(sock, answer_packet, buffer_size, 0, (struct sockaddr*) & request_address, sizeof(struct sockaddr)) == -1)
		{
			printf("本地构造未能成功发送至客户端！\n");
			return;
		}
		return;
	}
	else
	{
		//构造答案响应包
		memcpy(answer_packet + 6, &answer_number, sizeof(unsigned short));	//修改、添加信息，包括响应数、多出的响应字段等
		//资源记录数前面是问题数，直接使用客户端发来的请求包中的数据即可，不需要进行修改

		//构造DNS回答区域部分
		int curLen = 0;					//存储答案响应部分的长度
		char answer[256];				//答案包长度
		//由于可能存存在一个域名对应多个IP地址的情况
		for (int i = 0; i < ipNum; i++)
		{
			//Name域名部分
			//c00c=1100 0000 0000 1100，最高的两位为11，用于识别指针，其余的14位从DNS报文的开始处计数，指出该报文中的响应字节数
			unsigned short Name = htons(0xc00c);	//12正好是DNS报文协议头部的长度，之后就是查询问题区域，即所要进行查询的域名
			//htons函数把整型变量从主机字节顺序转变为网络字节顺序
			memcpy(answer+curLen, &Name, sizeof(unsigned short));
			curLen += sizeof(unsigned short);

			//查询类型Type
			unsigned short TypeA = htons(0x0001);//类型主机符A，由域名获得IPv4地址
			memcpy(answer + curLen, &TypeA, sizeof(unsigned short));
			curLen += sizeof(unsigned short);

			//查询类Class
			unsigned short ClassA = htons(0x0001);//查询类通常为1，表明是Internet数据
			memcpy(answer + curLen, &ClassA, sizeof(unsigned short));
			curLen += sizeof(unsigned short);

			//生存时间
			unsigned long timeLive = htons(0x7b);
			memcpy(answer + curLen, &timeLive, sizeof(unsigned long));
			curLen += sizeof(unsigned long);

			//资源数据长度
			//IP资源数据长度为4个字节
			char copy_ip[64];
			strcpy(copy_ip, ip[i]);
			copy_ip[strlen(ip)] = '\0';
			unsigned short IPLen = htons(0x0004);
			memcpy(answer + curLen, &IPLen, sizeof(unsigned short));
			curLen += sizeof(unsigned short);

			//Data资源数据
			unsigned long IP = (unsigned long)inet_addr(copy_ip);
			memcpy(answer + curLen, &IP, sizeof(unsigned long));
			curLen += sizeof(unsigned long);
		}
	
		//加上之前查询报文的长度
		curLen += buffer_size;
		//请求报文和响应部分共同组成DNS响应报文存入sendbuf
		memcpy(answer_packet + buffer_size, answer, curLen);

		if (sendto(sock, answer_packet, curLen, 0, (struct sockaddr*) & request_address, sizeof(struct sockaddr)) == -1)
		{
			printf("构造包未能成功发送至客户端!\n");
			return;
		}
	}

}

//交给上级服务器
/*
	@para
		buffer:请求的包
		request_address:请求地址
		buffer_size:请求包的大小
*/
void ask_next_server(char* buffer, struct sockaddr_in req_addr, int buffer_size)
{
	Req_inform tem;
	if(debug_level == 2)
		printf("本地域名解析表未找到，询问外部服务器\n");
	tem.client_addr = req_addr;	//请求端地址
	memcpy(&(tem.id), buffer, 2);	//原请求端id
	memcpy(buffer, &idThen, 2);	//赋予新的id号idThen
	if (sendto(sock, buffer, buffer_size, 0, (struct sockaddr*) & nser_addr, sizeof(struct sockaddr)) == -1)
	{
		printf("未发生成功至外部DNS服务器\n");
		return;
	}
	cache[idThen] = tem;	//增加本地id转换项至映射表项
	idThen = (idThen + 1) % idThen_max;
}

/*
	@para
		buffer:请求的包
		request_address:请求地址
		buffer_size:请求包的大小
*/
void deal_query(char* buffer, struct sockaddr_in req_addr, int buffer_size)
{
	if(debug_level == 2)
		printf("处理请求包中的url信息\n");

	//提取URL 从报文的第12个字节开始
	//从包里提取的域名格式为3www5baidu3com0
	char* url = get_url(buffer);

	unsigned short type = 0;
	memcpy(&type, buffer + buffer_size - 4, 2);
	int isIpv6 = 0;
	if (type == 0x0100)
	{
		if(debug_level==2)
			printf("IPV4查询包\n");
	}
	else if (type == 0x1c00)
	{
		if(debug_level == 2)
			printf("IPV6查询包\n");
		isIpv6 = 1;
	}
		
	else
		printf("未知查询包:%d\n",type);

	//本地中域名对应IP的个数
	int ipNum = 0;//本地ip返回数
	int ipNum2 = 0;//缓存ip返回数
	//初始化下标
	int ip_loc[5] = { -1,-1,-1,-1,-1 };
	int ip_loc2[5] = { -1,-1,-1,-1,-1 };
	//存储找到的ip信息
	char ipValue[5][32];
	char ipValue2[5][32];
	//是否在本地文件中
	ipNum = is_local(url, ip_loc);
	ipNum2 = is_cache(url, ip_loc2);
	//是否在缓存中
	for(int i = 0; i < ipNum; i++)
	{
		strcpy(ipValue[i], localdns[ip_loc[i]].ip);
	}
	for (int i = 0; i < ipNum2; i++)
	{
		strcpy(ipValue2[i],cachetable[ip_loc2[i]].ip);
	}
	if (ipNum)
	{//如果url在本地缓存
		printf("在本地缓存中查找到%d条信息\n", ipNum);
		for (int i = 0; i < ipNum; i++)
			printf("%s\n", ipValue[i]);
		create_respose(buffer, req_addr, buffer_size, ipValue,ipNum);
	}
	else if (ipNum2)
	{
		printf("在临时缓存中查找到%d条信息\n",ipNum2);
		for (int i = 0; i < ipNum2; i++)
			printf("%s\n", ipValue2[i]);
		create_respose(buffer, req_addr, buffer_size, ipValue2,ipNum2);
	}
	else
	{//如果不在，交给上级服务器处理
		printf("交给上级服务器...\n");
		ask_next_server(buffer, req_addr, buffer_size);
	}
}

/*
	@para
		url:请求的域名
		ip:从外部服务器得到的ip 
		ttl：保存时间
*/
void addCache(char* url, char* ip, int ttl)
{
	int minTime = time(NULL)+ttl;
	int pos = -1;
	int url_pos = -1;
	int time_pos = -1;
	//找出表中空闲位置和超时位置，优先替代超时位置的缓存。若两者都无， 则找出最先超时的位置
	for (int i = 0; i < 64; i++)
	{
		if (strcmp("", cachetable[i].url) == 0)
		{
			if(debug_level == 2)
				printf("位置%d空闲\n", i);
			if(url_pos < 0)
				url_pos = i;
		}
		else if (is_expired(cachetable[i].ttl))
		{
			if(debug_level == 2)
				printf("位置%d超时\n", i);
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
	//更新缓存
	if(debug_level == 2)
		printf("本次写入的位置:%d", pos);
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
		printf("从外部服务器得到响应包\n");
	memcpy(&cur_id, buffer, 2);

	//从ID缓存表中找到
	cur_inform = cache[cur_id];
	memcpy(buffer, &(cur_inform.id), 2);

	char* url = NULL;
	//获取问题个数和回答个数
	int queryNum = ntohs(*((unsigned short*)(buffer + 4)));
	int answerNum = ntohs(*((unsigned short*)(buffer + 6)));
	char* p = buffer + 12;

	if(debug_level == 2)
		printf("问题个数: %d, 回答个数: %d\n", queryNum, answerNum);
	for (int i = 0; i < queryNum; i++)
	{
		url = get_url(buffer);
		while (*p > 0)  //读取标识符前的计数跳过这个url
			p += (*p) + 1;
		p += 5; //跳过00和type、类信息
	}
	//分析回复
	//具体参考DNS回复报文格式
	int ip1, ip2, ip3, ip4;
	char ip[32];
	for (int i = 0; i < answerNum; ++i)
	{
		if ((unsigned char)* p == 0xc0) //是指针就跳过
			p += 2;
		else
		{
			//根据计数跳过url
			while (*p > 0)
				p += (*p) + 1;
			++p;    //指向后面的内容
		}
		unsigned short resp_type = ntohs(*(unsigned short*)p);  //回复类型
		p += 2;
		unsigned short resp_class = ntohs(*(unsigned short*)p); //回复类
		p += 2;
		unsigned short high = ntohs(*(unsigned short*)p);   //生存时间高位
		p += 2;
		unsigned short low = ntohs(*(unsigned short*)p);    //生存时间低位
		p += 2;
		int ttl = (((int)high) << 16) | low;    //高低位组合成生存时间
		int datalen = ntohs(*(unsigned short*)p);   //后面数据长度
		p += 2;

		if (resp_type == 1) //IPV4类型
		{
			//读取4个ip部分
			ip1 = (unsigned char)* p++;
			ip2 = (unsigned char)* p++;
			ip3 = (unsigned char)* p++;
			ip4 = (unsigned char)* p++;

			sprintf(ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);

			// 缓存从外部服务器中接受到的域名对应的IP
			addCache(url, ip, 10);
			break;
		}
		else p += datalen;  //直接跳过
	}
	if (sendto(sock, buffer, buffer_size, 0, (struct sockaddr*) & (cur_inform.client_addr), sizeof(struct sockaddr)) == -1)
	{
		printf("发送客户端失败\n");
	}
	else
		printf("发送客户端成功\n");
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
			printf("调试等级:%d\n", debug_level);
		}
		else
		{
			printf("%s\n", argv[i]);
			if (argv[i][0] >= '0' && argv[i][0] <= '9')
			{
				//设置外部服务器地址
				strcpy(nextServer, argv[i]);
				printf("下一站服务器:%s\n", nextServer);
			}
			else
			{
				//设置文件地址
				strcpy(file_name, argv[i]);
				printf("本地缓存文件:%s\n", file_name);
			}
			
		}
	}
}

/*
	本次更新:
	1、is_local和is_cache的查找函数增加了一个数组参数，用于保存下标，返回的是查找到的数量
	2、create_respose函数将原来的参数ip由一维数组变化到二维数组，ipNum为查找到的数量
		该函数结构未变多少，加了一个遍历的for循环
	3、将函数头定义放在了前面,删去了一些不必要的东西
*/