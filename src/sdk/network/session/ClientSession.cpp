//
// Created by focus on 31.05.2021.
//



#include "sdk/network/session/ClientSession.h"

#include <boost/asio/yield.hpp>

#include <algorithm>

namespace goodok {
    namespace detail {

        SocketWriter::SocketWriter(std::weak_ptr<socket_t> sock) :
            socketWeak_(std::move(sock))
        {
            log::write(log::Level::trace, "SocketWriter", "ctor done");
        }

        void SocketWriter::write(Serialize::Response const& /*message*/)
        {
            processWrite = !bufferWrite_.empty();
            detail::buffer_t data = MsgFactory::serialize<command::TypeCommand::AuthorisationResponse>(1);

            bufferWrite_.push_back(std::move(data));
            if (!processWrite) {
                writeImpl_();
            }
        }

        void SocketWriter::writeImpl_() {
            if (auto socket = socketWeak_.lock()) {
                auto callback = [selfWeak = weak_from_this()](boost::system::error_code, std::size_t)
                {
                    log::write(log::Level::info, "writeImpl_", "send");
                    if (auto self = selfWeak.lock()) {
                        self->bufferWrite_.pop_front();
                        if (!self->bufferWrite_.empty()) {
                            self->writeImpl_();
                        }
                    }
                };
                if (socket) {
                    boost::asio::async_write(
                            *socket,
                            boost::asio::buffer(bufferWrite_.front()),
                            std::move(callback));
                } else {
                    log::write(log::Level::error, "SocketWriter", "async_write socket is close");
                }

            } else {
                log::write(log::Level::error, "SocketWriter", "writeImpl_ socket is close");
            }
        }

    }

    ClientSession::ClientSession(AsyncContextWeakPtr ctxWeak, socket_t && socket) :
        ctx_(std::move(ctxWeak)),
        socket_(std::make_shared<socket_t>(std::move(socket))),
        writer_(std::make_shared<detail::SocketWriter>(detail::weak_from(socket_)))
    {
        if (!socket_){
            throw std::invalid_argument("incorrect socket");
        }
        if (!writer_) {
            throw std::invalid_argument("writer_ is failed");
        }
        log::write(log::Level::trace, "ClientSession", "ctor done");
    }

    ClientSession::~ClientSession()
    {
//        @TODO check when socket close
        if (socket_ && socket_->is_open()) {
            socket_->close();
        }
        log::write(log::Level::trace, "ClientSession", "dtor done");
    }

    void ClientSession::startRead()
    {
        runRead();
    }

    void ClientSession::runRead(boost::system::error_code ec, std::size_t)
    {
        if (ec) {
            log::write(log::Level::error,
                       "ClientSession",
                       boost::format("read with error: %1%") % ec.message());
            return;
        }

//        auto task = [selfWeak = weak_from_this()](Serialize::Header const& /*header*/, Serialize::Request const& request)
//        {
//            if (request.has_authorisation_request()) {
//                auto login = request.authorisation_request().login();
//                auto password = request.authorisation_request().password();
//                log::write(log::Level::debug, "ClientSession",
//                           boost::format("auth request: login=%1%, pass=%2%") % login % password);
//                if (auto self = selfWeak.lock()) {
//                    Serialize::Response response;
//                    self->write(response);
//                }
//            }
//        };

        auto callback = [selfWeak = weak_from_this()](boost::system::error_code ec, std::size_t nbytes) mutable {
            if (auto self = selfWeak.lock()) {
                self->runRead(ec, nbytes);
            }
        };

        reenter(coroData_.coro_) for(;;)
        {
            log::write(log::Level::debug, "ClientSession", "read header");
            yield boost::asio::async_read(*socket_,
                                          boost::asio::buffer(coroData_.bufferHeader_.data(), detail::MAX_SIZE_HEADER_BUFFER),
                                          callback);

            log::write(log::Level::debug, "ClientSession", "read body");

            coroData_.header.ParseFromArray(coroData_.bufferHeader_.data(), detail::MAX_SIZE_HEADER_BUFFER);

            coroData_.bufferBody_.resize(coroData_.header.length());
            yield boost::asio::async_read(*socket_,
                                          boost::asio::buffer(coroData_.bufferBody_.data(), coroData_.header.length()),
                                          callback);
            coroData_.request.ParseFromArray(coroData_.bufferBody_.data(),
                                             static_cast<int>(coroData_.header.length()));

            AsyncContext::runAsync(ctx_, &ClientSession::processRequest, this, coroData_.header, coroData_.request);
        }
    }

    void ClientSession::write(Serialize::Response const& message)
    {
        writer_->write(message);
    }

    void ClientSession::processRequest(Serialize::Header const& header, Serialize::Request const& request)
    {
        switch (static_cast<command::TypeCommand>(header.command()))
        {
            case command::TypeCommand::Unknown:
                log::write(log::Level::error, "processRequest", "Unknown command in header");
                break;
            case command::TypeCommand::RegistrationRequest:
                break;
            case command::TypeCommand::RegistrationResponse:
                break;
            case command::TypeCommand::AuthorisationRequest:
                log::write(log::Level::info, "processRequest", "Authorisation request");
                if (request.has_authorisation_request()) {
                    log::write(log::Level::debug, "processRequest", boost::format("login=%1%, password=%2%")
                        % request.authorisation_request().login() % request.authorisation_request().password());
                } else {
                    log::write(log::Level::error, "processRequest", "AuthorisationRequest: Mismatch command in header and type request in body");
                }
                break;
            case command::TypeCommand::AuthorisationResponse:
                break;
            case command::TypeCommand::SendTextRequest:
                break;
            case command::TypeCommand::EchoResponse:
                break;
            case command::TypeCommand::JoinRoomRequest:
                break;
            case command::TypeCommand::JoinRoomResponse:
                break;
            case command::TypeCommand::HistoryRequest:
                break;
            case command::TypeCommand::HistoryResponse:
                break;
            case command::TypeCommand::ChannelsRequest:
                break;
            case command::TypeCommand::ChannelsResponse:
                break;
            default:
                log::write(log::Level::error, "processRequest", "Failed command in header");
                break;
        }
    }

}