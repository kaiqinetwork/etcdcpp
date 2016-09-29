#ifndef __ETCD_CURL_HPP_INCLUDED__
#define __ETCD_CURL_HPP_INCLUDED__

#include <curl/curl.h>
#include <map>
#include <memory>
#include <sstream>
#include <string>

//#define DEBUG 1
//#define CRAZY_VERBOSE 1
//#define CRAZY_VERBOSE_STREAM stderr

namespace etcd {
namespace internal {

struct CurlUnknownException : public std::runtime_error {
    CurlUnknownException(const std::string& error)
       :std::runtime_error("curl unknown exception"),
        error(error)
      {}

    virtual const char* what() const throw() {
        return error.c_str();
    }

    std::string error;
};

struct CurlException : public std::runtime_error {
    CurlException(CURLcode errorCode,
                  const std::string& msg)
       :std::runtime_error("curl exception"),
        error_code(errorCode),
        msg(msg)
      {}

    virtual const char* what() const throw() {
        std::ostringstream estr;
        estr << msg << " [code: " << error_code << "] ";
        estr << curl_easy_strerror(error_code);
        return estr.str().c_str();
    }

    CURLcode error_code; 
    std::string msg;
};

typedef std::map<std::string, std::string> CurlOptions;

class Curl {
  public:
    // LIFECYCLE
    Curl();
    ~Curl();

    // OPERATIONS
    std::string Get(const std::string& url);

    std::string Set(const std::string& url,
             const std::string& type,
             const CurlOptions& options);

    std::string UrlEncode(const std::string& value);
    std::string UrlDecode(const std::string& value);

    void EnableHeader(bool onOff);

    std::string GetHeader();

    // callback from 'C' functions
    size_t WriteCb(void* buffer_p, size_t size, size_t nmemb) throw();
    size_t HeaderCb(void* buffer_p, size_t size, size_t nmemb) throw();

  private:
    // DATA MEMBERS
    CURL *handle_;
    std::ostringstream write_stream_;
    std::ostringstream header_stream_;
    bool enable_header_;

    // LIFECYCLE
    Curl(const Curl& rhs);
    void operator=(const Curl&& rhs);

    // OPERATIONS
    void _CheckError(CURLcode err, const std::string& msg);
    void _ResetHandle();

    void _SetCommonOptions(const std::string& url);

    void _SetGetOptions(const std::string& url);

    void _SetPostOptions(const std::string& url,
                         const std::string& type,
                         const CurlOptions& options);
};

extern "C" size_t
_WriteCb(void* buffer_p, size_t size, size_t nmemb, internal::Curl* curl_p) {
    return curl_p->WriteCb(buffer_p, size, nmemb);
}

extern "C" size_t
_HeaderCb(void* buffer_p, size_t size, size_t nmemb, internal::Curl* curl_p) {
    return curl_p->HeaderCb(buffer_p, size, nmemb);
}

#ifdef DEBUG
extern "C" int
_CurlTrace(CURL *handle, curl_infotype type,
           char *data, size_t size,
           void *userp);
#endif

//------------------------------- LIFECYCLE ----------------------------------

Curl::
Curl()
  :handle_(NULL),
   enable_header_(false) {

    curl_global_init(CURL_GLOBAL_ALL);
    handle_ = curl_easy_init();
    if (! handle_) 
        throw CurlUnknownException("failed init");
}

Curl::
~Curl() {
    curl_easy_cleanup(handle_);
}

//------------------------------- OPERATIONS ---------------------------------

std::string Curl::
Get(const std::string& url) {
    _ResetHandle();
    _SetGetOptions(url);

    CURLcode err = curl_easy_perform(handle_);
    _CheckError(err, "easy perform");

    return write_stream_.str();
}

std::string Curl::
Set(const std::string& url,
    const std::string& type,
    const CurlOptions& options) {

    _ResetHandle();
    _SetPostOptions(url, type, options);

    CURLcode err = curl_easy_perform(handle_);
    _CheckError(err, "easy perform");

    return write_stream_.str();
}

std::string Curl::
UrlEncode(const std::string& value) {
    char* encoded = curl_easy_escape(handle_, value.c_str(), (int)value.length());
    std::string retval (encoded);
    curl_free(encoded);
    return retval;
}

std::string Curl::
UrlDecode(const std::string& value) {
    int out_len;
    char* decoded = curl_easy_unescape(handle_,
            value.c_str(), (int) value.length(), &out_len);
    std::string retval (decoded, size_t(out_len));
    curl_free(decoded);
    return retval;
}

void Curl::
EnableHeader(bool onOff) {
    enable_header_ = onOff;
}

std::string Curl::
GetHeader() {
    return header_stream_.str();
}

size_t Curl::
WriteCb(void* buffer_p, size_t size, size_t nmemb) throw() {
    write_stream_ << std::string ((char*) buffer_p, size * nmemb);
    if (write_stream_.fail())
        return 0;
    return size * nmemb;
}

size_t Curl::
HeaderCb(void* buffer_p, size_t size, size_t nmemb) throw() {
    header_stream_ << std::string ((char*) buffer_p, size * nmemb);
    if (header_stream_.fail())
        return 0;
    return size * nmemb;
}

inline void Curl::
_CheckError(CURLcode err, const std::string& msg) {
    if (err != CURLE_OK) {
        throw CurlException(err, std::string("Failed ") + msg);
    }
}

void Curl::
_ResetHandle() {
    curl_easy_reset(handle_);
#ifdef  DEBUG
    curl_easy_setopt(handle_, CURLOPT_VERBOSE, 1L);
#ifdef  CRAZY_VERBOSE
    curl_easy_setopt(handle_, CURLOPT_DEBUGFUNCTION, _CurlTrace);
#endif
#endif
}

void Curl::
_SetCommonOptions(const std::string& url) {

    // set url
    CURLcode err = curl_easy_setopt(handle_, CURLOPT_URL, url.c_str());
    _CheckError(err, "set url");

    // Allow redirection
    err = curl_easy_setopt(handle_, CURLOPT_FOLLOWLOCATION, 1L);
    _CheckError(err, "set follow location");

    // Clear write stream
    write_stream_.str("");
    write_stream_.clear();

    // Set callback for write
    err = curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, _WriteCb);
    _CheckError(err, "set write callback");

    // Set callback data
    err = curl_easy_setopt(handle_, CURLOPT_WRITEDATA, this);
    _CheckError(err, "set write data");

    if (enable_header_) {
        // Get curl header
        
        // clear existing header data
        header_stream_.str("");
        header_stream_.clear();

        // Set header callback function
        err = curl_easy_setopt(handle_, CURLOPT_HEADERFUNCTION, _HeaderCb);
        _CheckError(err, "set header callback");

        // Set header user data for callback function
        err = curl_easy_setopt(handle_, CURLOPT_HEADERDATA, this);
        _CheckError(err, "set header data");
    }
 
    // Set the user agent. Some servers requires this on requests
    err = curl_easy_setopt(handle_, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    _CheckError(err, "set write data");
}

void Curl::
_SetGetOptions(const std::string& url) {
    _SetCommonOptions(url);
}

void Curl::
_SetPostOptions(
    const std::string& url,
    const std::string& type,
    const CurlOptions& options) {

    CURLcode err;
    err = curl_easy_setopt(handle_, CURLOPT_CUSTOMREQUEST, type.c_str());
    _CheckError(err, "set request type");

    _SetCommonOptions(url);

    err = curl_easy_setopt(handle_, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);
    _CheckError(err, "set post redir");

    std::ostringstream ostr;
    for (auto const& opt :options) {
        ostr << opt.first << '=' << opt.second << ';';
    }

    std::string opts (ostr.str());
    if (! opts.empty()) {
        err = curl_easy_setopt(handle_, CURLOPT_POST, 1L);
        _CheckError(err, "set post");
        err = curl_easy_setopt(handle_, CURLOPT_COPYPOSTFIELDS, opts.c_str());
        _CheckError(err, "set copy post fields");
    }
}

#ifdef CRAZY_VERBOSE
static
void dump(const char *text,
          FILE *stream, unsigned char *ptr, size_t size) {
  size_t i;
  size_t c;
  unsigned int width=0x10;
 
  fprintf(stream, "%s, %10.10ld bytes (0x%8.8lx)\n",
          text, (long)size, (long)size);
 
  for(i=0; i<size; i+= width) {
    fprintf(stream, "%4.4lx: ", (long)i);
 
    /* show hex to the left */
    for(c = 0; c < width; c++) {
      if(i+c < size)
        fprintf(stream, "%02x ", ptr[i+c]);
      else
        fputs("   ", stream);
    }
 
    /* show data on the right */
    for(c = 0; (c < width) && (i+c < size); c++)
      fputc((ptr[i+c]>=0x20) && (ptr[i+c]<0x80)?ptr[i+c]:'.', stream);
 
    fputc('\n', stream); /* newline */
  }
}
 
static
int _CurlTrace(CURL *handle, curl_infotype type,
             char *data, size_t size,
             void *userp) {
  const char *text;
  (void)handle; /* prevent compiler warning */
 
  switch (type) {
  case CURLINFO_TEXT:
    fprintf(stderr, "== Info: %s", data);
  default: /* in case a new one is introduced to shock us */
    return 0;
 
  case CURLINFO_HEADER_OUT:
    text = "=> Send header";
    break;
  case CURLINFO_DATA_OUT:
    text = "=> Send data";
    break;
  case CURLINFO_SSL_DATA_OUT:
    text = "=> Send SSL data";
    break;
  case CURLINFO_HEADER_IN:
    text = "<= Recv header";
    break;
  case CURLINFO_DATA_IN:
    text = "<= Recv data";
    break;
  case CURLINFO_SSL_DATA_IN:
    text = "<= Recv SSL data";
    break;
  }
 
  dump(text, CRAZY_VERBOSE_STREAM, (unsigned char *)data, size);
  return 0;
}
#endif // CRAZY_VERBOSE

} // namespace internal
} // namespace etcd

#endif // __ETCD_CURL_HPP_INCLUDED__