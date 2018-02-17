//
// Created by doom on 11/01/18.
//

#ifndef ZIA_HTTPCONNECTION_HPP
#define ZIA_HTTPCONNECTION_HPP

#include <functional>
#include <boost/system/error_code.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>
#include <api/net.h>
#include <utils/WeakCallback.hpp>
#include <utils/HTTPLengthExtractor.hpp>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace zia
{
    class HTTPConnection : public boost::enable_shared_from_this<HTTPConnection>
    {
        using ThisType = HTTPConnection;
        using ConnectionID = api::ImplSocket *;

        logging::Logger _log{"connections", logging::Debug};

    public:
        using Pointer = boost::shared_ptr<ThisType>;

        template <typename ...Args>
        static auto makeShared(Args &&...args) noexcept
        {
            return boost::make_shared<ThisType>(std::forward<Args>(args)...);
        }

#define logID(lvl)      _log(lvl) << this << ": "

        explicit HTTPConnection(asio::io_context &ctx) noexcept : _sock(ctx)
        {
            logID(logging::Debug) << "new connection" << std::endl;
        }

        ~HTTPConnection() noexcept
        {
            logID(logging::Debug) << "closing connection" << std::endl;
        }

        tcp::socket &socket() noexcept
        {
            return _sock;
        }

        const tcp::socket &socket() const noexcept
        {
            return _sock;
        }

        void readRequest(const std::function<void(const boost::system::error_code &ec,
                                                  zia::api::Net::Raw, zia::api::NetInfo)> &func) noexcept
        {
            _reqCallback = func;
            _parser.restart();
            std::vector<std::byte> save = std::move(_extraBytes);
            _extraBytes.clear();
            _handleSuccessfulReadHeader(save.begin(), save.end());
        }

    private:
        void _notifyError(const boost::system::error_code &ec) noexcept
        {
            _reqCallback(ec, {}, _getNetInfo());
        }

        void _readHeader() noexcept
        {
            _sock.async_read_some(asio::buffer(_buffer),
                                  utils::makeWeakCallback(shared_from_this(),
                                                          boost::bind(&HTTPConnection::_handleReadHeader,
                                                                      this, asio::placeholders::error,
                                                                      asio::placeholders::bytes_transferred)));
        }

#define failWithError(err)                                                  \
    {                                                                       \
        using namespace boost::system::errc;                                \
        _notifyError(make_error_code(err));                                 \
        return;                                                             \
    }

        template <typename Iterator>
        void _handleSuccessfulReadHeader(Iterator begin, Iterator end) noexcept
        {
            auto result = _parser.feed(begin, end);

            if (result.first == HTTPLengthExtractor::Result::ParseError) {
                failWithError(protocol_error);
            }

            if (result.first == HTTPLengthExtractor::Result::ParseSuccess) {
                try {
                    _targetSize = std::stoul(_parser.contentLength());
                } catch (const std::exception &e) {
                    failWithError(protocol_error);
                }
                _bodyBuffer.clear();
                if (result.second != end) {
                    auto alreadyRead = static_cast<size_t>(std::distance(result.second, end));
                    if (_targetSize <= alreadyRead) {
                        std::copy(result.second, result.second + _targetSize, std::back_inserter(_bodyBuffer));
                        std::copy(result.second + _targetSize, end, std::back_inserter(_extraBytes));
                        _targetSize = 0;
                    } else {
                        std::copy(result.second, end, std::back_inserter(_bodyBuffer));
                        _targetSize -= alreadyRead;
                    }
                }
                if (_targetSize > 0) {
                    _readBody();
                } else
                    _handleRequest();
            } else
                _readHeader();
        }

#undef failWithError

        void _handleReadHeader(const boost::system::error_code &ec, size_t bytesRead)
        {
            if (!ec) {
                _handleSuccessfulReadHeader(_buffer.begin(), _buffer.begin() + bytesRead);
            } else
                _notifyError(ec);
        }

        void _readBody() noexcept
        {
            _sock.async_read_some(asio::buffer(_buffer),
                                  utils::makeWeakCallback(shared_from_this(),
                                                          boost::bind(&HTTPConnection::_handleReadBody,
                                                                      this, asio::placeholders::error,
                                                                      asio::placeholders::bytes_transferred)));
        }

        void _handleReadBody(const boost::system::error_code &ec, size_t bytesRead)
        {
            if (!ec) {
                if (_targetSize <= bytesRead) {
                    std::move(_buffer.begin(), _buffer.begin() + _targetSize, std::back_inserter(_bodyBuffer));
                    std::move(_buffer.begin() + _targetSize, _buffer.begin() + bytesRead,
                              std::back_inserter(_extraBytes));
                    _handleRequest();
                } else {
                    std::move(_buffer.begin(), _buffer.begin() + bytesRead, std::back_inserter(_bodyBuffer));
                    _targetSize -= bytesRead;
                    _readBody();
                }
            } else
                _notifyError(ec);
        }

        api::NetInfo _getNetInfo() noexcept
        {
            api::NetInfo netInfo{};
            boost::system::error_code ec;
            auto endpoint = _sock.remote_endpoint(ec);
            if (!ec)
                netInfo.ip.str = endpoint.address().to_string();
            netInfo.sock = reinterpret_cast<ConnectionID>(this);
            return netInfo;
        }

        void _handleRequest() noexcept
        {
            std::vector<std::byte> reqBytes;
            reqBytes.reserve(_parser.buffer().size() + _bodyBuffer.size());

            std::move(_parser.buffer().begin(), _parser.buffer().end(), std::back_inserter(reqBytes));
            std::move(_bodyBuffer.begin(), _bodyBuffer.end(), std::back_inserter(reqBytes));
            _reqCallback({}, reqBytes, _getNetInfo());
        }

#undef logID

        HTTPLengthExtractor _parser;
        std::array<std::byte, 1024> _buffer;
        size_t _targetSize;
        std::vector<std::byte> _bodyBuffer;
        std::vector<std::byte> _extraBytes;
        tcp::socket _sock;
        std::function<void(const boost::system::error_code &, api::Net::Raw, api::NetInfo)> _reqCallback;
    };
}

#endif //ZIA_HTTPCONNECTION_HPP
