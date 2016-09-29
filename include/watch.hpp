#ifndef __ETCD_WATCH_HPP_INCLUDED__
#define __ETCD_WATCH_HPP_INCLUDED__

#include "client.hpp"

#ifndef MAX_FAILURES
#define MAX_FAILURES 5
#endif

namespace etcd {

/**
 * @brief A watch abstraction for monitoring a key or directory
 *
 * @tparam Reply json reply wrapper
 */
template <typename Reply>
class Watch {
  public:
    // TYPES
    typedef std::function <void (const Reply& r)> Callback;
   
    // LIFECYCLE
    /**
     * @brief Create a etcd::Watch object without authentication
     *
     * @param server etcd client URL without the port
     * @param port etcd client port
     */
    Watch(const std::string& server, const Port& port);

    /**
     * @brief Start the watch on a specific key or directory
     *
     * @param key key or directory to watch
     * @param callback call back when there is a change
     * @param prevIndex index value to start a watch from
     *
     * This function assumes you already know the current state of the key
     *
     * It handles index out of date by performing a GET and using X-Etcd-Index
     * filed from the header to start a new watch. The callback is also invoked
     * with the response from GET.
     *
     * It handles empty reply (generated when etcd server is going down or
     * cluster is getting reinitialized?) and tries to restart a watch upto
     * MAX_FAILURE failures in a row.
     */
    void Run(const std::string& key,
             Callback callback,
             const Index& prevIndex = 0);

    /**
     * @brief Start the watch on a specific key or directory. This will return
     * immmediately after a first change. It is the user's responsibility to
     * reschedule a watch. modifiedIndex will be stored by the API
     *
     * @param key key or directory to watch
     * @param callback call back when there is a change
     * @param prevIndex index value to start a watch from
     *
     * This function assumes you already know the current state of the key
     *
     * It handles index out of date by performing a GET and using X-Etcd-Index
     * filed from the header to start a new watch. The callback is also invoked
     * with the response from GET.
     *
     * It handles empty reply (generated when etcd server is going down and
     * throws etcd::ClientException
     */
    void RunOnce(const std::string& key,
             Callback callback,
             const Index& prevIndex = 0);

  private:
    // DATA MEMBERS
    Index prev_index_;
    std::string url_prefix_;
    std::unique_ptr<internal::Curl> handle_;
};

//------------------------------- LIFECYCLE ----------------------------------

template <typename Reply> Watch<Reply>::
Watch(const std::string& server, const Port& port)
try:
    handle_(new internal::Curl()),
    prev_index_(0) {
    std::ostringstream ostr;
    ostr << "http://" << server << ":" << port << "/v2/keys";
    url_prefix_ = ostr.str();
} catch (const std::exception& e) {
    throw ClientException(e.what());
}

//------------------------------- OPERATIONS ---------------------------------

template <typename Reply> void Watch<Reply>::
Run(const std::string& key, Watch::Callback callback, const Index& prevIndex) {
    const std::string watch_url_base = url_prefix_ + key + "?wait=true";
    const std::string wait_url_base = watch_url_base + "&waitIndex=";

    std::string watch_url = watch_url_base;

    if (prevIndex) {
        prev_index_ = prevIndex;
        watch_url += std::to_string(prev_index_ + 1);
    } else if (prev_index_) {
        watch_url += std::to_string(prev_index_ + 1);
    }

    int max_failures = MAX_FAILURES;

    while (max_failures) {
        try {
            // Watch for a change
            std::string ret = handle_->Get(watch_url);

            // Construct a reply and invoke the callback
            Reply r(ret);
            callback(r);

            // Update the prevIndex and the watch url
            prev_index_ = r.get_modified_index();
            watch_url = wait_url_base + std::to_string(prev_index_ + 1);

            // reset failures on a successful watch response
            max_failures = MAX_FAILURES;

        } catch (const ReplyException& e) {
            if (e.error_code == 401) {
                // We got an index out of date.
                try {
                // Enable curl headers
                handle_->EnableHeader(true);

                // Get the current state and call back
                std::string ret = handle_->Get(url_prefix_ + key);
                Reply r(ret);
                callback(r);

                // Get the new index from the curl header and start a watch
                std::istringstream stream(handle_->GetHeader());
                std::string etcd_index_label("X-Etcd-Index: ");
                std::string::size_type len = etcd_index_label.length();
                std::string line;
                std::string::size_type pos;
                while (std::getline(stream, line)) {
                    if ((pos = line.find(etcd_index_label) != std::string::npos))
                    {
                        prev_index_ = std::stoi(line.substr(pos+len-1));
                        break;
                    }
                }
                handle_->EnableHeader(false);
                watch_url = wait_url_base + std::to_string(prev_index_ + 1);
                } catch (...) {}
            }
            max_failures--; // still consider as a failure
        } catch (const std::exception& e) {
            // Possibly timed out and we didn't get a previous index
            // ToDo check timout options for libcurl
            max_failures--;
        }
    }
    if (! max_failures) {
        throw ClientException("watch failed or timedout");
    }
    return;
}

template <typename Reply> void Watch<Reply>::
RunOnce(
    const std::string& key,
    Watch::Callback callback,
    const Index& prevIndex) {

    const std::string watch_url_base = url_prefix_ + key + "?wait=true";
    const std::string wait_url_base = watch_url_base + "&waitIndex=";

    std::string watch_url = watch_url_base;

    if (prevIndex) {
        prev_index_ = prevIndex;
        watch_url += std::to_string(prev_index_ + 1);
    } else if (prev_index_) {
        watch_url += std::to_string(prev_index_ + 1);
    }

    try {
        // Watch for a change
        std::string ret = handle_->Get(watch_url);

        // Construct a reply and invoke the callback
        Reply r(ret);
        callback(r);

        // Update the prevIndex and the watch url
        prev_index_ = r.get_modified_index();
        watch_url = wait_url_base + std::to_string(prev_index_ + 1);

    } catch (const ReplyException& e) {
        if (e.error_code == 401) {
            // We got an index out of date.
            try {
            // Enable curl headers
            handle_->EnableHeader(true);

            // Get the current state and call back
            std::string ret = handle_->Get(url_prefix_ + key);
            Reply r(ret);
            callback(r);

            // Get the new index from the curl header and start a watch
            std::istringstream stream(handle_->GetHeader());
            std::string etcd_index_label("X-Etcd-Index: ");
            std::string::size_type len = etcd_index_label.length();
            std::string line;
            std::string::size_type pos;
            while (std::getline(stream, line)) {
                if ((pos = line.find(etcd_index_label) != std::string::npos))
                {
                    prev_index_ = std::stoi(line.substr(pos+len-1));
                    break;
                }
            }
            handle_->EnableHeader(false);
            watch_url = wait_url_base + std::to_string(prev_index_ + 1);
            } catch (...) {}
        }
    } catch (const std::exception& e) {
        throw ClientException("failed with" + std::string (e.what()));
    }
}

} // namespace etcd

#endif // __ETCD_WATCH_HPP_INCLUDED__