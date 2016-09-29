#include "client.hpp"
#include "rapid_reply.hpp"
#include <iconv/iconv.h>

#define GB2312	"gb2312"
#define GBK		"gbk"
#define UTF8	"utf-8"

std::string GBKToUTF8(const char *str)
{
	if (NULL == str || '\0' == str[0])
		return "";

	iconv_t cd = iconv_open(UTF8, GBK);
	if ((iconv_t)-1 == cd)
		return "";

	size_t inbytes = strlen(str);
	size_t outbytes = inbytes * 4;
	char* outbuf = (char*)malloc(outbytes);

#if _LIBICONV_VERSION <= 0x0109
	const char* in = str;
#else
	char* in = const_cast<char*&>(str);
#endif
	char* out = outbuf;
	memset(outbuf, 0, outbytes);

	std::string ret = "";
	if (iconv(cd, &in, &inbytes, &out, &outbytes) != (size_t)-1)
		ret = outbuf;
	free(outbuf);

	iconv_close(cd);

	return ret;
}

std::string UTF8ToGBK(const char *str)
{
	if (NULL == str || '\0' == str[0])
		return "";

	iconv_t cd = iconv_open(GBK, UTF8);
	if ((iconv_t)-1 == cd)
		return "";

	size_t inbytes = strlen(str);
	size_t outbytes = inbytes * 4;
	char* outbuf = (char*)malloc(outbytes);

#if _LIBICONV_VERSION <= 0x0109
	const char* in = str;
#else
	char* in = const_cast<char*&>(str);
#endif
	char* out = outbuf;
	memset(outbuf, 0, outbytes);

	std::string ret = "";
	if (iconv(cd, &in, &inbytes, &out, &outbytes) != (size_t)-1)
		ret = outbuf;
	free(outbuf);

	iconv_close(cd);

	return ret;
}

int main()
{
	etcd::Client<etcd::RapidReply> client("172.16.1.10", 2379);
	std::string a = GBKToUTF8("你好");
	
	etcd::RapidReply reply = client.Set("/message", a.c_str());
	etcd::RapidReply reply2 = client.Get("/message1");
	etcd::RapidReply::KvPairs result;
	reply2.GetAll(result);
	std::string b = UTF8ToGBK(result["/message1"].c_str());

	return 0;
}